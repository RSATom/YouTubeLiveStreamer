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

        const char* source = nullptr;
        if(CONFIG_TRUE == config_lookup_string(&config, "source", &source)) {
            loadedConfig.source = source;
        }
        const char* key = nullptr;
        if(CONFIG_TRUE == config_lookup_string(&config, "youtube-stream-key", &key)) {
            loadedConfig.youTubeStreamKey = key;
        }
        int logLevel = 0;
        if(CONFIG_TRUE == config_lookup_int(&config, "log-level", &logLevel)) {
            if(logLevel > 0) {
                loadedConfig.logLevel =
                    static_cast<spdlog::level::level_enum>(
                        spdlog::level::critical - std::min<int>(logLevel, spdlog::level::critical));
            }
        }
    }

    bool success = true;

    if(loadedConfig.source.empty()) {
        Log()->error("Missing source");
        success = false;
    }

    if(loadedConfig.youTubeStreamKey.empty()) {
        Log()->error("Missing key");
        success = false;
    }

    if(success)
        *config = loadedConfig;

    return success;
}

struct Context {
    std::unique_ptr<ReStreamer> reStreamer;
};

void StopReStream(Context* context)
{
    context->reStreamer.reset();
}

void ScheduleStartRestreawm(const Config&, Context*);

void StartRestream(const Config& config, Context* context)
{
    StopReStream(context);

    Log()->info("Restreaming \"{}\"", config.source);

    context->reStreamer =
        std::make_unique<ReStreamer>(
            config.source,
            "rtmp://a.rtmp.youtube.com/live2/" + config.youTubeStreamKey,
            [&config, context] () {
                ScheduleStartRestreawm(config, context);
            });

    context->reStreamer->start();
}

void ScheduleStartRestreawm(const Config& config, Context* context)
{
    Log()->info("Restreaming restart pending...");

    auto reconnect =
        [] (gpointer userData) -> gboolean {
             auto* data =
                reinterpret_cast<std::tuple<const Config&, Context*>*>(userData);

             StartRestream(std::get<0>(*data), std::get<1>(*data));

             delete data;

             return false;
        };

    auto* data = new std::tuple<const Config&, Context*>(config, context);

    g_timeout_add_seconds(RECONNECT_INTERVAL, GSourceFunc(reconnect), data);
}

int main(int argc, char *argv[])
{
    Config config {};
    if(!LoadConfig(&config))
        return -1;

    gst_init(&argc, &argv);

    GMainLoopPtr loopPtr(g_main_loop_new(nullptr, FALSE));
    GMainLoop* loop = loopPtr.get();

    Context context;

    StartRestream(config, &context);

    g_main_loop_run(loop);

    return 0;
}
