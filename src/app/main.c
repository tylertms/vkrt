#include "cli/cli.h"
#include "debug.h"
#include "editor/editor.h"
#include "mesh/controller.h"
#include "render/benchmark.h"
#include "render/controller.h"
#include "scene/controller.h"
#include "session.h"
#include "vkrt.h"
#include "vkrt_overlay.h"
#include "vkrt_types.h"

#include <stdint.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    CLILaunchOptions launchOptions = {0};
    char cliError[512];
    if (!CLIParseArguments(argc, argv, &launchOptions, cliError, sizeof(cliError))) {
        CLIPrintArgumentError(cliError);
        return EXIT_FAILURE;
    }

    int earlyExitCode = EXIT_SUCCESS;
    if (CLIHandleImmediateMode(&launchOptions, &earlyExitCode)) return earlyExitCode;

    benchmarkPrepareLaunchOptions(&launchOptions);

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
        exitCode = EXIT_FAILURE;
        goto cleanup;
    }

    if (launchOptions.loadDefaultScene) {
        if (!sceneControllerLoadDefaultScene(vkrt, &session)) {
            LOG_ERROR("Loading default scene failed");
            exitCode = EXIT_FAILURE;
            goto cleanup;
        }
    }

    if (launchOptions.startupImportPath) {
        if (!meshControllerImportMesh(vkrt, &session, launchOptions.startupImportPath, NULL, NULL)) {
            LOG_ERROR("Startup mesh import failed. Path: %s", launchOptions.startupImportPath);
            exitCode = EXIT_FAILURE;
            goto cleanup;
        }
    }

    if (benchmarkMode) {
        exitCode = benchmarkRun(vkrt, &launchOptions.benchmark);
        goto cleanup;
    }

    while (!VKRT_shouldDeinit(vkrt)) {
        VKRT_poll(vkrt);

        editorUIProcessDialogs(&session);
        sceneControllerApplySessionActions(vkrt, &session);
        meshControllerApplySessionActions(vkrt, &session);
        renderControllerApplySessionActions(vkrt, &session);
        editorUIUpdate(vkrt, &session);

        if (VKRT_draw(vkrt) != VKRT_SUCCESS) {
            LOG_ERROR("Frame render failed");
            exitCode = EXIT_FAILURE;
            break;
        }
    }

cleanup:
    VKRT_destroy(vkrt);
    sessionDeinit(&session);
    return exitCode;
}
