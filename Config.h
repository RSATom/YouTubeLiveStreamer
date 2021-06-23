#pragma once

#include <spdlog/common.h>


struct Config
{
    spdlog::level::level_enum logLevel = spdlog::level::info;

    std::string source;
    std::string youTubeStreamKey;
};
