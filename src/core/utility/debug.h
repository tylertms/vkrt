#pragma once

#include <stdint.h>
#include <stdio.h>

#define VKRT_LOG_LINE(stream, level, ...)    \
    do {                                      \
        fprintf((stream), level ": ");        \
        fprintf((stream), __VA_ARGS__);       \
        fputc('\n', (stream));                \
    } while (0)

#ifndef VKRT_TRACE_ENABLED
#define VKRT_TRACE_ENABLED 0
#endif

#if VKRT_TRACE_ENABLED
#define LOG_TRACE(...) VKRT_LOG_LINE(stdout, "[TRACE]", __VA_ARGS__)
#else
#define LOG_TRACE(...) do { if (0) { VKRT_LOG_LINE(stdout, "[TRACE]", __VA_ARGS__); } } while (0)
#endif

int vkrtInfoLoggingEnabled(void);
void vkrtSetInfoLoggingEnabled(int enabled);

#define LOG_INFO(...) do { if (vkrtInfoLoggingEnabled()) { VKRT_LOG_LINE(stdout, "[INFO]", __VA_ARGS__); } } while (0)
#define LOG_ERROR(...) VKRT_LOG_LINE(stderr, "[ERROR]", __VA_ARGS__)

uint64_t getMicroseconds(void);
