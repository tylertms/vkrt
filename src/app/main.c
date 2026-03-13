#include "cli/cli.h"
#include "session.h"
#include "render/sequencer.h"
#include "editor/editor.h"
#include "controller.h"
#include "vkrt.h"
#include "debug.h"

#include <inttypes.h>
#include <stdlib.h>

static const uint32_t kBenchmarkWarmupFrameCount = 2u;

int main(int argc, char* argv[]) {
    CLILaunchOptions launchOptions = {0};
    char cliError[512];
    if (!CLIParseArguments(argc, argv, &launchOptions, cliError, sizeof(cliError))) {
        CLIPrintArgumentError(cliError);
        return EXIT_FAILURE;
    }

    int earlyExitCode = EXIT_SUCCESS;
    if (CLIHandleImmediateMode(&launchOptions, &earlyExitCode)) return earlyExitCode;

    VKRT* vkrt = NULL;
    Session session = {0};
    int exitCode = EXIT_SUCCESS;

    if (VKRT_create(&vkrt) != VKRT_SUCCESS || !vkrt) {
        LOG_ERROR("Failed to allocate VKRT runtime");
        return EXIT_FAILURE;
    }

    sessionInit(&session);

    const uint8_t benchmarkMode = launchOptions.benchmark.enabled;
    if (!benchmarkMode) {
        VKRT_AppHooks hooks = {
            .init = editorUIInitialize,
            .deinit = editorUIShutdown,
            .drawOverlay = editorUIDraw,
            .userData = &session,
        };
        VKRT_registerAppHooks(vkrt, hooks);
    }

    if (VKRT_initWithCreateInfo(vkrt, &launchOptions.createInfo) != VKRT_SUCCESS) {
        LOG_ERROR("Failed to initialize VKRT runtime");
        VKRT_deinit(vkrt);
        VKRT_destroy(vkrt);
        sessionDeinit(&session);
        return EXIT_FAILURE;
    }

    if (launchOptions.loadDefaultScene) {
        meshControllerLoadDefaultAssets(vkrt, &session);
    }

    if (launchOptions.startupImportPath) {
        if (!meshControllerImportMesh(vkrt, &session, launchOptions.startupImportPath, NULL, NULL)) {
            LOG_ERROR("Startup mesh import failed. Path: %s", launchOptions.startupImportPath);
            VKRT_deinit(vkrt);
            VKRT_destroy(vkrt);
            sessionDeinit(&session);
            return EXIT_FAILURE;
        }
    }

    uint8_t benchmarkStarted = 0u;
    uint8_t benchmarkCompleted = 0u;
    uint32_t benchmarkWarmupFramesRemaining = benchmarkMode ? kBenchmarkWarmupFrameCount : 0u;
    uint64_t benchmarkStartTimeUs = 0u;
    if (benchmarkMode) {
        VKRT_SystemInfo systemInfo = {0};
        const char* deviceName = "(unknown device)";
        if (VKRT_getSystemInfo(vkrt, &systemInfo) == VKRT_SUCCESS && systemInfo.deviceName[0]) {
            deviceName = systemInfo.deviceName;
        }
        printf("Benchmarking default scene on %s at %ux%u for %u target samples\n",
            deviceName,
            launchOptions.benchmark.width,
            launchOptions.benchmark.height,
            launchOptions.benchmark.targetSamples);
    }

    while (!VKRT_shouldDeinit(vkrt)) {
        VKRT_poll(vkrt);

        if (!benchmarkMode) {
            editorUIProcessDialogs(&session);
            meshControllerApplySessionActions(vkrt, &session);
            renderSequencerHandleCommands(vkrt, &session);
            editorUIUpdate(vkrt, &session);
        } else if (!benchmarkStarted && benchmarkWarmupFramesRemaining == 0u) {
            if (VKRT_startRender(vkrt,
                    launchOptions.benchmark.width,
                    launchOptions.benchmark.height,
                    launchOptions.benchmark.targetSamples) != VKRT_SUCCESS) {
                LOG_ERROR("Failed to start benchmark render");
                exitCode = EXIT_FAILURE;
                break;
            }
            benchmarkStarted = 1u;
            benchmarkStartTimeUs = getMicroseconds();
        }

        if (VKRT_draw(vkrt) != VKRT_SUCCESS) {
            LOG_ERROR("Frame render failed");
            exitCode = EXIT_FAILURE;
            break;
        }

        if (benchmarkMode) {
            if (!benchmarkStarted) {
                if (benchmarkWarmupFramesRemaining > 0u) benchmarkWarmupFramesRemaining--;
                continue;
            }

            VKRT_RenderStatusSnapshot status = {0};
            if (VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS) {
                LOG_ERROR("Failed to query benchmark status");
                exitCode = EXIT_FAILURE;
                break;
            }

            if (status.renderModeFinished) {
                uint64_t benchmarkEndTimeUs = getMicroseconds();
                double elapsedSeconds = 0.0;
                if (benchmarkEndTimeUs >= benchmarkStartTimeUs) {
                    elapsedSeconds = (double)(benchmarkEndTimeUs - benchmarkStartTimeUs) / 1000000.0;
                }
                double samplesPerSecond = elapsedSeconds > 0.0
                    ? (double)status.totalSamples / elapsedSeconds
                    : 0.0;
                double millisecondsPerSample = status.totalSamples > 0
                    ? (elapsedSeconds * 1000.0) / (double)status.totalSamples
                    : 0.0;

                printf("Benchmark complete: %.3f s, %.2f samples/s, %.3f ms/sample, actual %" PRIu64 " samples (target %u)\n",
                    elapsedSeconds,
                    samplesPerSecond,
                    millisecondsPerSample,
                    status.totalSamples,
                    launchOptions.benchmark.targetSamples);
                benchmarkCompleted = 1u;
                break;
            }

            continue;
        }

        renderSequencerUpdate(vkrt, &session);

        char* savePath = NULL;
        if (sessionTakeRenderSave(&session, &savePath)) {
            if (VKRT_saveRenderPNG(vkrt, savePath) != VKRT_SUCCESS) {
                LOG_ERROR("Saving render PNG failed. Path: %s", savePath);
            }
            free(savePath);
        }
    }

    if (benchmarkMode && !benchmarkCompleted && exitCode == EXIT_SUCCESS) {
        LOG_ERROR("Benchmark did not complete");
        exitCode = EXIT_FAILURE;
    }

    VKRT_deinit(vkrt);
    VKRT_destroy(vkrt);
    sessionDeinit(&session);
    return exitCode;
}
