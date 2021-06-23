#include "ReStreamer.h"

#include <cassert>

#include <CxxPtr/GlibPtr.h>

#include "Log.h"


static const auto Log = ReStreamerLog;


ReStreamer::ReStreamer(
    const std::string& sourceUrl,
    const std::string& targetUrl,
    const std::function<void ()>& onEos) :
    _onEos(onEos), _sourceUrl(sourceUrl), _targetUrl(targetUrl)
{
}

ReStreamer::~ReStreamer()
{
    stop();
}

void ReStreamer::setState(GstState state) noexcept
{
    if(!_pipelinePtr) {
        if(state != GST_STATE_NULL)
            ;
        return;
    }

    GstElement* pipeline = _pipelinePtr.get();

    switch(gst_element_set_state(pipeline, state)) {
        case GST_STATE_CHANGE_FAILURE:
            break;
        case GST_STATE_CHANGE_SUCCESS:
            break;
        case GST_STATE_CHANGE_ASYNC:
            break;
        case GST_STATE_CHANGE_NO_PREROLL:
            break;
    }
}

void ReStreamer::pause() noexcept
{
    setState(GST_STATE_PAUSED);
}

void ReStreamer::play() noexcept
{
    setState(GST_STATE_PLAYING);
}

void ReStreamer::stop() noexcept
{
    setState(GST_STATE_NULL);
}

gboolean ReStreamer::onBusMessage(GstMessage* message)
{
    switch(GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_EOS:
            onEos(false);
            break;
        case GST_MESSAGE_ERROR: {
            gchar* debug;
            GError* error;

            gst_message_parse_error(message, &error, &debug);

            g_free(debug);
            g_error_free(error);

            onEos(true);
            break;
        }
        case GST_MESSAGE_APPLICATION: {
            const GstStructure* structure =
                gst_message_get_structure(message);
            assert(structure);
            if(!structure)
                break;

            if(gst_message_has_name(message, "eos")) {
                gboolean error = FALSE;
                gst_structure_get_boolean(structure, "error", &error);
                onEos(error != FALSE);
            }
            break;
        }
        default:
            break;
    }

    return TRUE;
}

void ReStreamer::onEos(bool /*error*/)
{
    _onEos();
}


// called from streaming thread
void ReStreamer::postEos(
    GstElement* rtcbin,
    gboolean error)
{
    GstStructure* structure =
        gst_structure_new(
            "eos",
            "error", G_TYPE_BOOLEAN, error,
            nullptr);

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(rtcbin), structure);

    GstBusPtr busPtr(gst_element_get_bus(rtcbin));
    gst_bus_post(busPtr.get(), message);
}

void ReStreamer::start() noexcept
{
    GstElementPtr pipelinePtr(gst_pipeline_new(nullptr));
    GstElement* pipeline = pipelinePtr.get();
    if(!pipeline) {
        Log()->error("Failed to create pipeline element");
        return;
    }

    GstElementPtr srcPtr(gst_element_factory_make("uridecodebin", nullptr));
    GstElement* decodebin = srcPtr.get();
    if(!decodebin) {
        Log()->error("Failed to create \"uridecodebin\" element");
        return;
    }

    _flvMuxPtr.reset(gst_element_factory_make("flvmux", "mux"));
    GstElement* flvMux = _flvMuxPtr.get();
    if(!flvMux) {
        Log()->error("Failed to create \"flvmux\" element");
        return;
    }

    GstElementPtr rtmpSinkPtr(gst_element_factory_make("rtmpsink", nullptr));
    GstElement* rtmpSink = rtmpSinkPtr.get();
    if(!rtmpSink) {
        Log()->error("Failed to create \"rtmpsink\" element");
        return;
    }


    _h264CapsPtr.reset(gst_caps_from_string("video/x-h264"));
    _audioRawCapsPtr.reset(gst_caps_from_string("audio/x-raw"));

    GstCapsPtr supportedCapsPtr(gst_caps_copy(_h264CapsPtr.get()));
    gst_caps_append(supportedCapsPtr.get(), gst_caps_copy(_audioRawCapsPtr.get()));
    GstCaps* supportedCaps = supportedCapsPtr.get();

    g_object_set(decodebin, "caps", supportedCaps, nullptr);

    auto onBusMessageCallback =
        (gboolean (*) (GstBus*, GstMessage*, gpointer))
        [] (GstBus* bus, GstMessage* message, gpointer userData) -> gboolean
    {
        ReStreamer* self = static_cast<ReStreamer*>(userData);
        return self->onBusMessage(message);
    };
    GstBusPtr busPtr(gst_pipeline_get_bus(GST_PIPELINE(pipeline)));
    gst_bus_add_watch(busPtr.get(), onBusMessageCallback, this);

    auto srcPadAddedCallback =
        (void (*)(GstElement*, GstPad*, gpointer))
         [] (GstElement* decodebin, GstPad* pad, gpointer userData)
    {
        ReStreamer* self = static_cast<ReStreamer*>(userData);
        self->srcPadAdded(decodebin, pad);
    };
    g_signal_connect(decodebin, "pad-added", G_CALLBACK(srcPadAddedCallback), this);

    auto noMorePadsCallback =
        (void (*)(GstElement*,  gpointer))
         [] (GstElement* decodebin, gpointer userData)
    {
        ReStreamer* self = static_cast<ReStreamer*>(userData);
        self->noMorePads(decodebin);
    };
    g_signal_connect(decodebin, "no-more-pads", G_CALLBACK(noMorePadsCallback), this);

    g_object_set(decodebin,
        "uri", _sourceUrl.c_str(),
        nullptr);

    _flvVideoSinkPad.reset(gst_element_get_request_pad(_flvMuxPtr.get(), "video"));
    _flvAudioSinkPad.reset(gst_element_get_request_pad(_flvMuxPtr.get(), "audio"));

    g_object_set(flvMux, "streamable", true, nullptr);

    g_object_set(rtmpSink, "location", _targetUrl.c_str(), nullptr);

    gst_object_ref(flvMux);
    gst_bin_add_many(
        GST_BIN(pipeline),
        srcPtr.release(), flvMux, rtmpSinkPtr.release(),
        nullptr);
    gst_element_link_many(
        flvMux, rtmpSink,
        nullptr);

    _pipelinePtr = std::move(pipelinePtr);

    play();
}

void ReStreamer::srcPadAdded(
    GstElement* /*decodebin*/,
    GstPad* pad)
{
    GstElement* pipeline = _pipelinePtr.get();

    GstCapsPtr capsPtr(gst_pad_get_current_caps(pad));
    GstCaps* caps = capsPtr.get();

    if(gst_caps_is_always_compatible(caps, _h264CapsPtr.get())) {
        if(_videoLinked) {
            Log()->error("Multiple video streams not supported");
            return;
        }

        if(GST_PAD_LINK_OK != gst_pad_link(pad, _flvVideoSinkPad.get()))
            assert(false);

        _videoLinked = true;
    } else if(gst_caps_is_always_compatible(caps, _audioRawCapsPtr.get())) {
        if(_audioLinked) {
            Log()->error("Multiple audio streams not supported");
            return;
        }

        GstElementPtr audioResamplePtr(gst_element_factory_make("audioresample", nullptr));
        GstElement* audioResample = audioResamplePtr.get();
        if(!audioResample) {
            Log()->error("Failed to create \"audioresample\" element");
            return;
        }

        gst_bin_add(GST_BIN(pipeline), audioResamplePtr.release());
        gst_element_sync_state_with_parent(audioResample);

        GstPadPtr resampleSinkPad(gst_element_get_static_pad(audioResample, "sink"));
        GstPadPtr resampleSrcPad(gst_element_get_static_pad(audioResample, "src"));

        if(GST_PAD_LINK_OK != gst_pad_link(pad, resampleSinkPad.get()))
            assert(false);

        if(GST_PAD_LINK_OK != gst_pad_link(resampleSrcPad.get(), _flvAudioSinkPad.get()))
            assert(false);

        _audioLinked = true;
    } else
        return;
}


void ReStreamer::noMorePads(GstElement* /*decodebin*/)
{
    if(!_audioLinked) {
        // YouTube ignores video without audio, so just stream silence.

        GstElement* pipeline = _pipelinePtr.get();

        GstElementPtr audioTestSrcPtr(gst_element_factory_make("audiotestsrc", nullptr));
        GstElement* audioTestSrc = audioTestSrcPtr.get();
        if(!audioTestSrc) {
            Log()->error("Failed to create \"audiotestsrc\" element");
            return;
        }

        gst_bin_add(GST_BIN(pipeline), audioTestSrcPtr.release());
        gst_element_sync_state_with_parent(audioTestSrc);

        gst_util_set_object_arg(G_OBJECT(audioTestSrc), "wave", "silence");

        GstPadPtr audioTestSrcPad(gst_element_get_static_pad(audioTestSrc, "src"));

        if(GST_PAD_LINK_OK != gst_pad_link(audioTestSrcPad.get(), _flvAudioSinkPad.get()))
            assert(false);

        _audioLinked = true;
    }
}
