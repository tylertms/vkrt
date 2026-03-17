#include "benchmark.h"

#include "debug.h"

#include <inttypes.h>
#include <stdlib.h>

static const uint32_t kBenchmarkSetupFrameCount = 2u;
static const uint32_t kBenchmarkWarmupSamples = 512u;
static const uint64_t kBenchmarkWarmupTimeUs = 2000000u;

typedef struct BenchmarkState {
    uint8_t renderStarted;
    uint8_t timingStarted;
    uint32_t setupFramesRemaining;
    uint32_t warmupSamples;
    uint32_t lockedSamplesPerFrame;
    uint64_t renderStartTimeUs;
    uint64_t measurementSamplesStart;
    uint64_t startTimeUs;
} BenchmarkState;

static uint32_t queryBenchmarkWarmupSamples(uint32_t targetSamples) {
    uint32_t warmupSamples = kBenchmarkWarmupSamples;
    if (targetSamples > 0u && warmupSamples * 2u > targetSamples) {
        warmupSamples = targetSamples / 4u;
    }
    return warmupSamples > 0u ? warmupSamples : 1u;
}

static const char* queryBenchmarkModeName(const CLIBenchmarkOptions* options) {
    return options && options->headless ? "headless" : "windowed";
}

static double normalizeBenchmarkSeconds(double elapsedSeconds, uint64_t actualSamples, uint32_t targetSamples) {
    if (elapsedSeconds <= 0.0 || actualSamples == 0u || targetSamples == 0u) return 0.0;
    return elapsedSeconds * (double)targetSamples / (double)actualSamples;
}

static int configureBenchmarkWarmup(VKRT* vkrt) {
    return VKRT_setAutoSPPEnabled(vkrt, 1u) == VKRT_SUCCESS;
}

static int lockBenchmarkSampling(VKRT* vkrt, uint32_t* outSamplesPerFrame) {
    if (!vkrt || !outSamplesPerFrame) return 0;

    VKRT_SceneSettingsSnapshot settings = {0};
    if (VKRT_getSceneSettings(vkrt, &settings) != VKRT_SUCCESS) {
        return 0;
    }

    uint32_t samplesPerFrame = settings.samplesPerPixel > 0u ? settings.samplesPerPixel : 1u;
    if (VKRT_setAutoSPPEnabled(vkrt, 0u) != VKRT_SUCCESS ||
        VKRT_setSamplesPerPixel(vkrt, samplesPerFrame) != VKRT_SUCCESS) {
        return 0;
    }

    *outSamplesPerFrame = samplesPerFrame;
    return 1;
}

void benchmarkPrepareLaunchOptions(CLILaunchOptions* options) {
    if (!options || !options->benchmark.enabled) return;

    options->createInfo.headless = options->benchmark.headless;
    if (options->benchmark.headless) {
        options->createInfo.width = options->benchmark.width;
        options->createInfo.height = options->benchmark.height;
        options->createInfo.startMaximized = 0u;
        options->createInfo.startFullscreen = 0u;
    }

    vkrtSetInfoLoggingEnabled(0);
}

int benchmarkRun(VKRT* vkrt, const CLIBenchmarkOptions* options) {
    if (!vkrt || !options || !options->enabled) return EXIT_FAILURE;
    if (!configureBenchmarkWarmup(vkrt)) {
        LOG_ERROR("Failed to configure benchmark warmup");
        return EXIT_FAILURE;
    }

    BenchmarkState state = {
        .setupFramesRemaining = kBenchmarkSetupFrameCount,
        .warmupSamples = queryBenchmarkWarmupSamples(options->targetSamples),
    };

    VKRT_SystemInfo systemInfo = {0};
    const char* deviceName = "(unknown device)";
    if (VKRT_getSystemInfo(vkrt, &systemInfo) == VKRT_SUCCESS && systemInfo.deviceName[0]) {
        deviceName = systemInfo.deviceName;
    }

    printf(
        "Benchmark %s: %s, %ux%u, target %u samples\n",
        queryBenchmarkModeName(options),
        deviceName,
        options->width,
        options->height,
        options->targetSamples
    );

    for (;;) {
        VKRT_poll(vkrt);
        if (!options->headless && VKRT_shouldDeinit(vkrt)) {
            LOG_ERROR("Benchmark aborted after window close");
            return EXIT_FAILURE;
        }

        if (!state.renderStarted && state.setupFramesRemaining == 0u) {
            if (VKRT_startRender(vkrt, options->width, options->height, UINT32_MAX) != VKRT_SUCCESS) {
                LOG_ERROR("Failed to start benchmark render");
                return EXIT_FAILURE;
            }
            state.renderStarted = 1u;
            state.renderStartTimeUs = getMicroseconds();
        }

        if (VKRT_draw(vkrt) != VKRT_SUCCESS) {
            LOG_ERROR("Frame render failed");
            return EXIT_FAILURE;
        }

        if (!state.renderStarted) {
            if (state.setupFramesRemaining > 0u) state.setupFramesRemaining--;
            continue;
        }

        VKRT_RenderStatusSnapshot status = {0};
        if (VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS) {
            LOG_ERROR("Failed to query benchmark status");
            return EXIT_FAILURE;
        }

        uint64_t nowUs = getMicroseconds();
        int warmupSamplesReached = status.totalSamples >= state.warmupSamples;
        int warmupTimeReached = state.renderStartTimeUs > 0u &&
                                nowUs >= state.renderStartTimeUs &&
                                nowUs - state.renderStartTimeUs >= kBenchmarkWarmupTimeUs;
        if (!state.timingStarted && warmupSamplesReached && warmupTimeReached) {
            if (!lockBenchmarkSampling(vkrt, &state.lockedSamplesPerFrame)) {
                LOG_ERROR("Failed to lock benchmark sampling");
                return EXIT_FAILURE;
            }
            state.timingStarted = 1u;
            state.measurementSamplesStart = status.totalSamples;
            state.startTimeUs = getMicroseconds();
            continue;
        }

        uint64_t measuredSamples = status.totalSamples;
        if (measuredSamples >= state.measurementSamplesStart) {
            measuredSamples -= state.measurementSamplesStart;
        } else {
            measuredSamples = 0u;
        }

        if (state.timingStarted && measuredSamples >= options->targetSamples) {
            double elapsedSeconds = 0.0;
            if (nowUs >= state.startTimeUs) {
                elapsedSeconds = (double)(nowUs - state.startTimeUs) / 1000000.0;
            }
            double normalizedSeconds = normalizeBenchmarkSeconds(
                elapsedSeconds,
                measuredSamples,
                options->targetSamples
            );
            double samplesPerSecond = elapsedSeconds > 0.0
                ? (double)measuredSamples / elapsedSeconds
                : 0.0;
            double millisecondsPerSample = measuredSamples > 0
                ? (elapsedSeconds * 1000.0) / (double)measuredSamples
                : 0.0;

            printf(
                "Benchmark complete: %.3f s, %.2f samples/s, %.3f ms/sample, %u spp/frame, actual %" PRIu64 " samples\n",
                normalizedSeconds,
                samplesPerSecond,
                millisecondsPerSample,
                state.lockedSamplesPerFrame,
                measuredSamples
            );
            return EXIT_SUCCESS;
        }

        if (status.renderModeFinished) {
            LOG_ERROR("Benchmark render finished before reaching the timed sample target");
            return EXIT_FAILURE;
        }
    }
}
