#include <string>
#include <deque>

#include <gst/gst.h>

#include "CxxPtr/GlibPtr.h"
#include "CxxPtr/libconfigDestroy.h"

#include "Log.h"
#include "Config.h"
#include "ConfigHelpers.h"
#include "ReStreamer.h"

enum {
    RECONNECT_INTERVAL = 5
};

static const auto Log = ReStreamerLog;


static bool LoadConfig(Config* config)
{
    const std::deque<std::string> configDirs = ::ConfigDirs();
    if(configDirs.empty())
        return false;

    Config loadedConfig = *config;

    for(const std::string& configDir: configDirs) {
        const std::string configFile = configDir + "/live-streamer.conf";
        if(!g_file_test(configFile.c_str(),  G_FILE_TEST_IS_REGULAR)) {
            Log()->info("Config \"{}\" not found", configFile);
            continue;
        }

        config_t config;
        config_init(&config);
        ConfigDestroy ConfigDestroy(&config);

        Log()->info("Loading config \"{}\"", configFile);
        if(!config_read_file(&config, configFile.c_str())) {
            Log()->error("Fail load config. {}. {}:{}",
                config_error_text(&config),
                configFile,
                config_error_line(&config));
            return false;
        }

        int logLevel = 0;
        if(CONFIG_TRUE == config_lookup_int(&config, "log-level", &logLevel)) {
            if(logLevel > 0) {
                loadedConfig.logLevel =
                    static_cast<spdlog::level::level_enum>(
                        spdlog::level::critical - std::min<int>(logLevel, spdlog::level::critical));
            }
        }

        const char* source = nullptr;
        config_lookup_string(&config, "source", &source);
        const char* key = nullptr;
        config_lookup_string(&config, "youtube-stream-key", &key);

        if(source && key) {
            loadedConfig.reStreamers.emplace_back(Config::ReStreamer{ source, key });
        }

        config_setting_t* streamersConfig = config_lookup(&config, "streamers");
        if(streamersConfig && CONFIG_TRUE == config_setting_is_list(streamersConfig)) {
            const int streamersCount = config_setting_length(streamersConfig);
            for(int streamerIdx = 0; streamerIdx < streamersCount; ++streamerIdx) {
                config_setting_t* streamerConfig =
                    config_setting_get_elem(streamersConfig, streamerIdx);
                if(!streamerConfig || CONFIG_FALSE == config_setting_is_group(streamerConfig)) {
                    Log()->warn("Wrong streamer config format. Streamer skipped.");
                    break;
                }

                const char* source = nullptr;
                config_setting_lookup_string(streamerConfig, "source", &source);
                const char* key = nullptr;
                config_setting_lookup_string(streamerConfig, "youtube-stream-key", &key);

                if(source && key) {
                    loadedConfig.reStreamers.emplace_back(Config::ReStreamer{ source, key });
                }
            }
        }
    }

    bool success = true;
    if(loadedConfig.reStreamers.empty()) {
        Log()->error("No streamers configured");
        success = false;
    }

    if(success)
        *config = loadedConfig;

    return success;
}

struct ReStreamContext {
    std::unique_ptr<ReStreamer> reStreamer;
};

void StopReStream(ReStreamContext* context)
{
    context->reStreamer.reset();
}

void ScheduleStartRestreawm(const Config::ReStreamer&, ReStreamContext*);

void StartRestream(const Config::ReStreamer& reStreamer, ReStreamContext* context)
{
    StopReStream(context);

    Log()->info("Restreaming \"{}\"", reStreamer.source);

    context->reStreamer =
        std::make_unique<ReStreamer>(
            reStreamer.source,
            "rtmp://a.rtmp.youtube.com/live2/" + reStreamer.youTubeStreamKey,
            [&reStreamer, context] () {
                ScheduleStartRestreawm(reStreamer, context);
            });

    context->reStreamer->start();
}

void ScheduleStartRestreawm(const Config::ReStreamer& reStreamer, ReStreamContext* context)
{
    Log()->info("ReStreaming restart pending...");

    auto reconnect =
        [] (gpointer userData) -> gboolean {
             auto* data =
                reinterpret_cast<std::tuple<const Config::ReStreamer&, ReStreamContext*>*>(userData);

             StartRestream(std::get<0>(*data), std::get<1>(*data));

             return false;
        };

    auto* data = new std::tuple<const Config::ReStreamer&, ReStreamContext*>(reStreamer, context);

    g_timeout_add_seconds_full(
        G_PRIORITY_DEFAULT,
        RECONNECT_INTERVAL,
        GSourceFunc(reconnect),
        data,
        [] (gpointer userData) {
             auto* data =
                reinterpret_cast<std::tuple<const Config::ReStreamer&, ReStreamContext*>*>(userData);
             delete data;
        });
}

int main(int argc, char *argv[])
{
    Config config {};
    if(!LoadConfig(&config))
        return -1;

    gst_init(&argc, &argv);

    GMainLoopPtr loopPtr(g_main_loop_new(nullptr, FALSE));
    GMainLoop* loop = loopPtr.get();

    std::deque<ReStreamContext> contexts;

    for(const Config::ReStreamer& reStreamer: config.reStreamers) {
        ReStreamContext& context = *contexts.emplace(contexts.end());

        StartRestream(reStreamer, &context);
    }

    g_main_loop_run(loop);

    return 0;
}
