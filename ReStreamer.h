#pragma once

#include <memory>
#include <string>
#include <functional>

#include <CxxPtr/GstPtr.h>

class ReStreamer
{
public:
    ReStreamer(
        const std::string& sourceUrl,
        const std::string& targetUrl,
        const std::function<void ()>& onEos);
    ~ReStreamer();

    void start() noexcept;

private:
    void setState(GstState) noexcept;
    void pause() noexcept;
    void play() noexcept;
    void stop() noexcept;

    gboolean onBusMessage(GstMessage*);

    void unknownType(
        GstElement* decodebin,
        GstPad*,
        GstCaps*);
    void srcPadAdded(GstElement* decodebin, GstPad*);
    void noMorePads(GstElement* decodebin);

    static void postEos(
        GstElement* rtcbin,
        gboolean error);

    void onEos(bool error);

private:
    std::function<void ()> _onEos;

    const std::string _sourceUrl;
    const std::string _targetUrl;

    GstElementPtr _pipelinePtr;
    GstElementPtr _flvMuxPtr;
    GstPadPtr _flvVideoSinkPad;
    GstPadPtr _flvAudioSinkPad;

    GstCapsPtr _h264CapsPtr;
    GstCapsPtr _audioRawCapsPtr;

    bool _videoLinked = false;
    bool _audioLinked = false;
};
