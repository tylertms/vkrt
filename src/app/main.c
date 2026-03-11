#include "cli/cli.h"
#include "session.h"
#include "render/sequencer.h"
#include "editor/editor.h"
#include "controller.h"
#include "vkrt.h"
#include "debug.h"

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

    VKRT* vkrt = NULL;
    Session session = {0};
    int exitCode = EXIT_SUCCESS;

    if (VKRT_create(&vkrt) != VKRT_SUCCESS || !vkrt) {
        LOG_ERROR("Failed to allocate VKRT runtime");
        return EXIT_FAILURE;
    }

    sessionInit(&session);

    VKRT_AppHooks hooks = {
        .init = editorUIInitialize,
        .deinit = editorUIShutdown,
        .drawOverlay = editorUIDraw,
        .userData = &session,
    };
    VKRT_registerAppHooks(vkrt, hooks);

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

    while (!VKRT_shouldDeinit(vkrt)) {
        VKRT_poll(vkrt);
        editorUIProcessDialogs(&session);
        meshControllerApplySessionActions(vkrt, &session);
        renderSequencerHandleCommands(vkrt, &session);
        editorUIUpdate(vkrt, &session);

        if (VKRT_draw(vkrt) != VKRT_SUCCESS) {
            LOG_ERROR("Frame render failed");
            exitCode = EXIT_FAILURE;
            break;
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

    VKRT_deinit(vkrt);
    VKRT_destroy(vkrt);
    sessionDeinit(&session);
    return exitCode;
}
