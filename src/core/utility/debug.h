#pragma once

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#if defined(__MINGW32__) || defined(__MINGW64__)
#define VKRT_PRINTF_ATTRIBUTE(formatIndex, firstArg) __attribute__((format(gnu_printf, formatIndex, firstArg)))
#else
#define VKRT_PRINTF_ATTRIBUTE(formatIndex, firstArg) __attribute__((format(printf, formatIndex, firstArg)))
#endif
void vkrtLogLine(FILE* stream, const char* level, const char* format, ...) VKRT_PRINTF_ATTRIBUTE(3, 4);
#else
void vkrtLogLine(FILE* stream, const char* level, const char* format, ...);
#endif

#define VKRT_LOG_LINE(stream, level, ...) vkrtLogLine((stream), (level), __VA_ARGS__)

#ifndef VKRT_TRACE_ENABLED
#define VKRT_TRACE_ENABLED 0
#endif

#if VKRT_TRACE_ENABLED
#define LOG_TRACE(...) VKRT_LOG_LINE(stdout, "[TRACE]", __VA_ARGS__)
#else
#define LOG_TRACE(...)                                     \
    do {                                                   \
        if (0) {                                           \
            VKRT_LOG_LINE(stdout, "[TRACE]", __VA_ARGS__); \
        }                                                  \
    } while (0)
#endif

int vkrtInfoLoggingEnabled(void);
void vkrtSetInfoLoggingEnabled(int enabled);

#define LOG_INFO(...)                                     \
    do {                                                  \
        if (vkrtInfoLoggingEnabled()) {                   \
            VKRT_LOG_LINE(stdout, "[INFO]", __VA_ARGS__); \
        }                                                 \
    } while (0)
#define LOG_ERROR(...) VKRT_LOG_LINE(stderr, "[ERROR]", __VA_ARGS__)

uint64_t getMicroseconds(void);

#if defined(__GNUC__) || defined(__clang__)
#undef VKRT_PRINTF_ATTRIBUTE
#endif

#ifdef __cplusplus
}
#endif
