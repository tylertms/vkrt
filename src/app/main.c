#include "cli/cli.h"
#include "debug.h"
#include "editor/editor.h"
#include "mesh/controller.h"
#include "render/benchmark.h"
#include "render/controller.h"
#include "scene/controller.h"
#include "session.h"
#include "vkrt.h"
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

    offlineRenderPrepareLaunchOptions(&launchOptions);

    VKRT* vkrt = NULL;
    Session session = {0};
    int exitCode = EXIT_SUCCESS;

    if (VKRT_create(&vkrt) != VKRT_SUCCESS || !vkrt) {
        LOG_ERROR("Failed to allocate VKRT runtime");
        return EXIT_FAILURE;
    }

    sessionInit(&session);

    const uint8_t offlineRenderMode = launchOptions.offlineRender.enabled;
    if (!offlineRenderMode) {
        editorRegisterAppHooks(vkrt, &session);
    }

    if (VKRT_initWithCreateInfo(vkrt, &launchOptions.createInfo) != VKRT_SUCCESS) {
        LOG_ERROR("Failed to initialize VKRT runtime");
        exitCode = EXIT_FAILURE;
        goto cleanup;
    }

    if (
        !sceneControllerLoadStartupScene(vkrt, &session, launchOptions.startupScenePath, launchOptions.loadDefaultScene)
    ) {
        if (launchOptions.startupScenePath) {
            LOG_ERROR("Loading startup scene failed. Path: %s", launchOptions.startupScenePath);
        } else {
            LOG_ERROR("Loading default scene failed");
        }
        exitCode = EXIT_FAILURE;
        goto cleanup;
    }

    if (launchOptions.startupImportPath &&
        !meshControllerImportMesh(vkrt, &session, launchOptions.startupImportPath, NULL, NULL)) {
        LOG_ERROR("Startup mesh import failed. Path: %s", launchOptions.startupImportPath);
        exitCode = EXIT_FAILURE;
        goto cleanup;
    }

    if (offlineRenderMode) {
        exitCode = offlineRenderRun(vkrt, &launchOptions.offlineRender);
        if (exitCode == EXIT_SUCCESS && launchOptions.renderOutputPath) {
            exitCode = offlineRenderSaveOutput(vkrt, launchOptions.renderOutputPath);
        }
        goto cleanup;
    }

    exitCode = renderControllerRunInteractiveLoop(vkrt, &session);

cleanup:
    VKRT_destroy(vkrt);
    sessionDeinit(&session);
    return exitCode;
}
