#pragma once

#ifndef NDEBUG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#define TINE_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define TINE_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define TINE_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define TINE_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define TINE_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)

#define TINE_CHECK(err, msg, label)                                                                \
    if (!(err)) {                                                                                  \
        TINE_ERROR("{0} failed: {1}", #err, (msg));                                                \
        goto label;                                                                                \
    }
