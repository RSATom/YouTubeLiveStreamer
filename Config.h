#pragma once

#include <deque>

#include <spdlog/common.h>


struct Config
{
    struct ReStreamer;

    spdlog::level::level_enum logLevel = spdlog::level::info;

    std::deque<ReStreamer> reStreamers;
};

struct Config::ReStreamer {
    std::string source;
    std::string youTubeStreamKey;
};
