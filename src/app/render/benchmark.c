#include "benchmark.h"

#include "cli/cli.h"
#include "debug.h"
#include "vkrt.h"
#include "vkrt_types.h"

#include <stdint.h>
#include <stdio.h>
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

typedef enum BenchmarkStepResult {
    BENCHMARK_STEP_CONTINUE = 0,
    BENCHMARK_STEP_SUCCESS,
    BENCHMARK_STEP_FAILURE,
} BenchmarkStepResult;

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

static int beginBenchmarkRender(VKRT* vkrt, const CLIBenchmarkOptions* options, BenchmarkState* state) {
    if (!vkrt || !options || !state) return 0;
    if (VKRT_startRender(vkrt, options->width, options->height, UINT32_MAX) != VKRT_SUCCESS) {
        return 0;
    }
    state->renderStarted = 1u;
    state->renderStartTimeUs = getMicroseconds();
    return 1;
}

static int queryBenchmarkStatus(VKRT* vkrt, VKRT_RenderStatusSnapshot* status) {
    return vkrt && status && VKRT_getRenderStatus(vkrt, status) == VKRT_SUCCESS;
}

static int beginBenchmarkTiming(VKRT* vkrt, BenchmarkState* state, uint64_t totalSamples, uint64_t nowUs) {
    int warmupSamplesReached = 0;
    int warmupTimeReached = 0;

    if (!vkrt || !state || state->timingStarted) return 1;
    warmupSamplesReached = totalSamples >= state->warmupSamples;
    warmupTimeReached = state->renderStartTimeUs > 0u && nowUs >= state->renderStartTimeUs &&
                        nowUs - state->renderStartTimeUs >= kBenchmarkWarmupTimeUs;
    if (!warmupSamplesReached || !warmupTimeReached) return 1;
    if (!lockBenchmarkSampling(vkrt, &state->lockedSamplesPerFrame)) return 0;

    state->timingStarted = 1u;
    state->measurementSamplesStart = totalSamples;
    state->startTimeUs = getMicroseconds();
    return 1;
}

static uint64_t queryMeasuredSamples(const BenchmarkState* state, uint64_t totalSamples) {
    if (!state || totalSamples < state->measurementSamplesStart) return 0u;
    return totalSamples - state->measurementSamplesStart;
}

static void printBenchmarkResult(
    const BenchmarkState* state,
    const CLIBenchmarkOptions* options,
    uint64_t nowUs,
    uint64_t measuredSamples
) {
    double elapsedSeconds = 0.0;
    double normalizedSeconds = 0.0;
    double samplesPerSecond = 0.0;
    double millisecondsPerSample = 0.0;

    if (state && nowUs >= state->startTimeUs) {
        elapsedSeconds = (double)(nowUs - state->startTimeUs) / 1000000.0;
    }
    normalizedSeconds =
        normalizeBenchmarkSeconds(elapsedSeconds, measuredSamples, options ? options->targetSamples : 0u);
    samplesPerSecond = elapsedSeconds > 0.0 ? (double)measuredSamples / elapsedSeconds : 0.0;
    millisecondsPerSample = measuredSamples > 0u ? (elapsedSeconds * 1000.0) / (double)measuredSamples : 0.0;

    printf(
        "Benchmark complete: %.3f s, %.2f samples/s, %.3f ms/sample, %u spp/frame, actual %llu "
        "samples\n",
        normalizedSeconds,
        samplesPerSecond,
        millisecondsPerSample,
        state ? state->lockedSamplesPerFrame : 0u,
        (unsigned long long)measuredSamples
    );
}

static void queryBenchmarkDeviceName(VKRT* vkrt, char* outName, size_t outNameSize) {
    VKRT_SystemInfo systemInfo = {0};
    if (!outName || outNameSize == 0u) return;

    outName[0] = '\0';
    if (VKRT_getSystemInfo(vkrt, &systemInfo) == VKRT_SUCCESS && systemInfo.deviceName[0]) {
        snprintf(outName, outNameSize, "%s", systemInfo.deviceName);
        return;
    }
    snprintf(outName, outNameSize, "%s", "(unknown device)");
}

static void printBenchmarkHeader(VKRT* vkrt, const CLIBenchmarkOptions* options) {
    char deviceName[256];

    queryBenchmarkDeviceName(vkrt, deviceName, sizeof(deviceName));
    printf(
        "Benchmark %s: %s, %ux%u, target %u samples\n",
        queryBenchmarkModeName(options),
        deviceName,
        options->width,
        options->height,
        options->targetSamples
    );
}

static int handleBenchmarkWindowClose(VKRT* vkrt, const CLIBenchmarkOptions* options) {
    if (options->headless || !VKRT_shouldDeinit(vkrt)) return 1;
    LOG_ERROR("Benchmark aborted after window close");
    return 0;
}

static int startBenchmarkRenderIfReady(VKRT* vkrt, const CLIBenchmarkOptions* options, BenchmarkState* state) {
    if (!vkrt || !options || !state) return 0;
    if (state->renderStarted || state->setupFramesRemaining > 0u) return 1;
    if (beginBenchmarkRender(vkrt, options, state)) return 1;
    LOG_ERROR("Failed to start benchmark render");
    return 0;
}

static int drawBenchmarkFrame(VKRT* vkrt) {
    if (VKRT_draw(vkrt) == VKRT_SUCCESS) return 1;
    LOG_ERROR("Frame render failed");
    return 0;
}

static BenchmarkStepResult advanceBenchmarkSetup(BenchmarkState* state) {
    if (!state || state->renderStarted) return BENCHMARK_STEP_CONTINUE;
    if (state->setupFramesRemaining > 0u) state->setupFramesRemaining--;
    return BENCHMARK_STEP_CONTINUE;
}

static BenchmarkStepResult finishBenchmarkIfTargetReached(
    const BenchmarkState* state,
    const CLIBenchmarkOptions* options,
    const VKRT_RenderStatusSnapshot* status,
    uint64_t nowUs
) {
    uint64_t measuredSamples = 0u;

    if (!state || !options || !status || !state->timingStarted) return BENCHMARK_STEP_CONTINUE;

    measuredSamples = queryMeasuredSamples(state, status->totalSamples);
    if (measuredSamples >= options->targetSamples) {
        printBenchmarkResult(state, options, nowUs, measuredSamples);
        return BENCHMARK_STEP_SUCCESS;
    }
    if (VKRT_renderStatusIsComplete(status)) {
        LOG_ERROR("Benchmark render finished before reaching the timed sample target");
        return BENCHMARK_STEP_FAILURE;
    }
    return BENCHMARK_STEP_CONTINUE;
}

static BenchmarkStepResult runBenchmarkIteration(
    VKRT* vkrt,
    const CLIBenchmarkOptions* options,
    BenchmarkState* state
) {
    VKRT_RenderStatusSnapshot status = {0};
    uint64_t nowUs = 0u;

    VKRT_poll(vkrt);
    if (!handleBenchmarkWindowClose(vkrt, options)) return BENCHMARK_STEP_FAILURE;
    if (!startBenchmarkRenderIfReady(vkrt, options, state)) return BENCHMARK_STEP_FAILURE;
    if (!drawBenchmarkFrame(vkrt)) return BENCHMARK_STEP_FAILURE;
    if (!state->renderStarted) return advanceBenchmarkSetup(state);

    if (!queryBenchmarkStatus(vkrt, &status)) {
        LOG_ERROR("Failed to query benchmark status");
        return BENCHMARK_STEP_FAILURE;
    }

    nowUs = getMicroseconds();
    if (!beginBenchmarkTiming(vkrt, state, status.totalSamples, nowUs)) {
        LOG_ERROR("Failed to lock benchmark sampling");
        return BENCHMARK_STEP_FAILURE;
    }
    if (state->timingStarted && (state->startTimeUs == 0u || state->startTimeUs > nowUs)) {
        return BENCHMARK_STEP_CONTINUE;
    }

    return finishBenchmarkIfTargetReached(state, options, &status, nowUs);
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
    BenchmarkStepResult stepResult = BENCHMARK_STEP_CONTINUE;

    if (!vkrt || !options || !options->enabled) return EXIT_FAILURE;
    if (!configureBenchmarkWarmup(vkrt)) {
        LOG_ERROR("Failed to configure benchmark warmup");
        return EXIT_FAILURE;
    }

    BenchmarkState state = {
        .setupFramesRemaining = kBenchmarkSetupFrameCount,
        .warmupSamples = queryBenchmarkWarmupSamples(options->targetSamples),
    };

    printBenchmarkHeader(vkrt, options);

    for (;;) {
        stepResult = runBenchmarkIteration(vkrt, options, &state);
        if (stepResult == BENCHMARK_STEP_SUCCESS) return EXIT_SUCCESS;
        if (stepResult == BENCHMARK_STEP_FAILURE) return EXIT_FAILURE;
    }
}
