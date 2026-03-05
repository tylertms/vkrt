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

#define LOG_INFO(...) VKRT_LOG_LINE(stdout, "[INFO]", __VA_ARGS__)
#define LOG_ERROR(...) VKRT_LOG_LINE(stderr, "[ERROR]", __VA_ARGS__)

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
static inline uint64_t getMicroseconds(void) {
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart * 1000000 / frequency.QuadPart);
}
#else
#include <time.h>
static inline uint64_t getMicroseconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
#endif
