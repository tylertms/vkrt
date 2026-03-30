#include "benchmark.h"

#include "cli/cli.h"
#include "debug.h"
#include "vkrt.h"
#include "vkrt_types.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static const uint32_t kOfflineRenderSetupFrameCount = 2u;
static const uint32_t kOfflineRenderWarmupSamples = 512u;
static const uint64_t kOfflineRenderWarmupTimeUs = 2000000u;

typedef struct OfflineRenderState {
    uint8_t renderStarted;
    uint8_t timingStarted;
    uint32_t setupFramesRemaining;
    uint32_t warmupSamples;
    uint32_t lockedSamplesPerFrame;
    uint64_t renderStartTimeUs;
    uint64_t measurementSamplesStart;
    uint64_t startTimeUs;
} OfflineRenderState;

typedef enum OfflineRenderStepResult {
    OFFLINE_RENDER_STEP_CONTINUE = 0,
    OFFLINE_RENDER_STEP_SUCCESS,
    OFFLINE_RENDER_STEP_FAILURE,
} OfflineRenderStepResult;

static uint32_t queryOfflineRenderWarmupSamples(uint32_t targetSamples) {
    uint32_t warmupSamples = kOfflineRenderWarmupSamples;
    if (targetSamples > 0u && warmupSamples * 2u > targetSamples) {
        warmupSamples = targetSamples / 4u;
    }
    return warmupSamples > 0u ? warmupSamples : 1u;
}

static const char* queryOfflineRenderModeName(const CLIOfflineRenderOptions* options) {
    return options && options->headless ? "headless" : "windowed";
}

static double normalizeOfflineRenderSeconds(double elapsedSeconds, uint64_t actualSamples, uint32_t targetSamples) {
    if (elapsedSeconds <= 0.0 || actualSamples == 0u || targetSamples == 0u) return 0.0;
    return elapsedSeconds * (double)targetSamples / (double)actualSamples;
}

static int configureOfflineRenderWarmup(VKRT* vkrt) {
    return VKRT_setAutoSPPEnabled(vkrt, 1u) == VKRT_SUCCESS;
}

static int lockOfflineRenderSampling(VKRT* vkrt, uint32_t* outSamplesPerFrame) {
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

static int beginOfflineRender(VKRT* vkrt, const CLIOfflineRenderOptions* options, OfflineRenderState* state) {
    if (!vkrt || !options || !state) return 0;
    if (VKRT_startRender(vkrt, options->width, options->height, UINT32_MAX) != VKRT_SUCCESS) {
        return 0;
    }
    state->renderStarted = 1u;
    state->renderStartTimeUs = getMicroseconds();
    return 1;
}

static int queryOfflineRenderStatus(VKRT* vkrt, VKRT_RenderStatusSnapshot* status) {
    return vkrt && status && VKRT_getRenderStatus(vkrt, status) == VKRT_SUCCESS;
}

static int beginOfflineRenderTiming(VKRT* vkrt, OfflineRenderState* state, uint64_t totalSamples, uint64_t nowUs) {
    int warmupSamplesReached = 0;
    int warmupTimeReached = 0;

    if (!vkrt || !state || state->timingStarted) return 1;
    warmupSamplesReached = totalSamples >= state->warmupSamples;
    warmupTimeReached = state->renderStartTimeUs > 0u && nowUs >= state->renderStartTimeUs &&
                        nowUs - state->renderStartTimeUs >= kOfflineRenderWarmupTimeUs;
    if (!warmupSamplesReached || !warmupTimeReached) return 1;
    if (!lockOfflineRenderSampling(vkrt, &state->lockedSamplesPerFrame)) return 0;

    state->timingStarted = 1u;
    state->measurementSamplesStart = totalSamples;
    state->startTimeUs = getMicroseconds();
    return 1;
}

static uint64_t queryMeasuredSamples(const OfflineRenderState* state, uint64_t totalSamples) {
    if (!state || totalSamples < state->measurementSamplesStart) return 0u;
    return totalSamples - state->measurementSamplesStart;
}

static void printOfflineRenderResult(
    const OfflineRenderState* state,
    const CLIOfflineRenderOptions* options,
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
        normalizeOfflineRenderSeconds(elapsedSeconds, measuredSamples, options ? options->targetSamples : 0u);
    samplesPerSecond = elapsedSeconds > 0.0 ? (double)measuredSamples / elapsedSeconds : 0.0;
    millisecondsPerSample = measuredSamples > 0u ? (elapsedSeconds * 1000.0) / (double)measuredSamples : 0.0;

    printf(
        "Offline render complete: %.3f s, %.2f samples/s, %.3f ms/sample, %u spp/frame, actual %llu "
        "samples\n",
        normalizedSeconds,
        samplesPerSecond,
        millisecondsPerSample,
        state ? state->lockedSamplesPerFrame : 0u,
        (unsigned long long)measuredSamples
    );
}

static void queryOfflineRenderDeviceName(VKRT* vkrt, char* outName, size_t outNameSize) {
    VKRT_SystemInfo systemInfo = {0};
    if (!outName || outNameSize == 0u) return;

    outName[0] = '\0';
    if (VKRT_getSystemInfo(vkrt, &systemInfo) == VKRT_SUCCESS && systemInfo.deviceName[0]) {
        (void)snprintf(outName, outNameSize, "%s", systemInfo.deviceName);
        return;
    }
    (void)snprintf(outName, outNameSize, "%s", "(unknown device)");
}

static void printOfflineRenderHeader(VKRT* vkrt, const CLIOfflineRenderOptions* options) {
    char deviceName[256];

    queryOfflineRenderDeviceName(vkrt, deviceName, sizeof(deviceName));
    printf(
        "Offline render %s: %s, %ux%u, target %u samples\n",
        queryOfflineRenderModeName(options),
        deviceName,
        options->width,
        options->height,
        options->targetSamples
    );
}

static int handleOfflineRenderWindowClose(VKRT* vkrt, const CLIOfflineRenderOptions* options) {
    if (options->headless || !VKRT_shouldDeinit(vkrt)) return 1;
    LOG_ERROR("Offline render aborted after window close");
    return 0;
}

static int startOfflineRenderIfReady(VKRT* vkrt, const CLIOfflineRenderOptions* options, OfflineRenderState* state) {
    if (!vkrt || !options || !state) return 0;
    if (state->renderStarted || state->setupFramesRemaining > 0u) return 1;
    if (beginOfflineRender(vkrt, options, state)) return 1;
    LOG_ERROR("Failed to start offline render");
    return 0;
}

static int drawOfflineRenderFrame(VKRT* vkrt) {
    if (VKRT_draw(vkrt) == VKRT_SUCCESS) return 1;
    LOG_ERROR("Frame render failed");
    return 0;
}

static OfflineRenderStepResult advanceOfflineRenderSetup(OfflineRenderState* state) {
    if (!state || state->renderStarted) return OFFLINE_RENDER_STEP_CONTINUE;
    if (state->setupFramesRemaining > 0u) state->setupFramesRemaining--;
    return OFFLINE_RENDER_STEP_CONTINUE;
}

static OfflineRenderStepResult finishOfflineRenderIfTargetReached(
    const OfflineRenderState* state,
    const CLIOfflineRenderOptions* options,
    const VKRT_RenderStatusSnapshot* status,
    uint64_t nowUs
) {
    uint64_t measuredSamples = 0u;

    if (!state || !options || !status || !state->timingStarted) return OFFLINE_RENDER_STEP_CONTINUE;

    measuredSamples = queryMeasuredSamples(state, status->totalSamples);
    if (measuredSamples >= options->targetSamples) {
        printOfflineRenderResult(state, options, nowUs, measuredSamples);
        return OFFLINE_RENDER_STEP_SUCCESS;
    }
    if (VKRT_renderStatusIsComplete(status)) {
        LOG_ERROR("Offline render finished before reaching the timed sample target");
        return OFFLINE_RENDER_STEP_FAILURE;
    }
    return OFFLINE_RENDER_STEP_CONTINUE;
}

static OfflineRenderStepResult runOfflineRenderIteration(
    VKRT* vkrt,
    const CLIOfflineRenderOptions* options,
    OfflineRenderState* state
) {
    VKRT_RenderStatusSnapshot status = {0};
    uint64_t nowUs = 0u;

    VKRT_poll(vkrt);
    if (!handleOfflineRenderWindowClose(vkrt, options)) return OFFLINE_RENDER_STEP_FAILURE;
    if (!startOfflineRenderIfReady(vkrt, options, state)) return OFFLINE_RENDER_STEP_FAILURE;
    if (!drawOfflineRenderFrame(vkrt)) return OFFLINE_RENDER_STEP_FAILURE;
    if (!state->renderStarted) return advanceOfflineRenderSetup(state);

    if (!queryOfflineRenderStatus(vkrt, &status)) {
        LOG_ERROR("Failed to query offline render status");
        return OFFLINE_RENDER_STEP_FAILURE;
    }

    nowUs = getMicroseconds();
    if (!beginOfflineRenderTiming(vkrt, state, status.totalSamples, nowUs)) {
        LOG_ERROR("Failed to lock offline render sampling");
        return OFFLINE_RENDER_STEP_FAILURE;
    }
    if (state->timingStarted && (state->startTimeUs == 0u || state->startTimeUs > nowUs)) {
        return OFFLINE_RENDER_STEP_CONTINUE;
    }

    return finishOfflineRenderIfTargetReached(state, options, &status, nowUs);
}

void offlineRenderPrepareLaunchOptions(CLILaunchOptions* options) {
    if (!options || !options->offlineRender.enabled) return;

    options->createInfo.headless = options->offlineRender.headless;
    if (options->offlineRender.headless) {
        options->createInfo.width = options->offlineRender.width;
        options->createInfo.height = options->offlineRender.height;
        options->createInfo.startMaximized = 0u;
        options->createInfo.startFullscreen = 0u;
    }

    vkrtSetInfoLoggingEnabled(0);
}

int offlineRenderSaveOutput(VKRT* vkrt, const char* outputPath) {
    if (!vkrt || !outputPath) return EXIT_FAILURE;

    VKRT_RenderExportSettings exportSettings = {0};
    VKRT_defaultRenderExportSettings(&exportSettings);
    exportSettings.denoiseEnabled = 0u;
    if (VKRT_saveRenderImageEx(vkrt, outputPath, &exportSettings) == VKRT_SUCCESS) {
        return EXIT_SUCCESS;
    }

    LOG_ERROR("Saving offline render failed. Path: %s", outputPath);
    return EXIT_FAILURE;
}

int offlineRenderRun(VKRT* vkrt, const CLIOfflineRenderOptions* options) {
    OfflineRenderStepResult stepResult = OFFLINE_RENDER_STEP_CONTINUE;

    if (!vkrt || !options || !options->enabled) return EXIT_FAILURE;
    if (!configureOfflineRenderWarmup(vkrt)) {
        LOG_ERROR("Failed to configure offline render warmup");
        return EXIT_FAILURE;
    }

    OfflineRenderState state = {
        .setupFramesRemaining = kOfflineRenderSetupFrameCount,
        .warmupSamples = queryOfflineRenderWarmupSamples(options->targetSamples),
    };

    printOfflineRenderHeader(vkrt, options);

    for (;;) {
        stepResult = runOfflineRenderIteration(vkrt, options, &state);
        if (stepResult == OFFLINE_RENDER_STEP_SUCCESS) return EXIT_SUCCESS;
        if (stepResult == OFFLINE_RENDER_STEP_FAILURE) return EXIT_FAILURE;
    }
}
