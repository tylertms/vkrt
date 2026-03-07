#include "cli/cli.h"
#include "session.h"
#include "render/sequencer.h"
#include "editor/editor.h"
#include "controller.h"
#include "vkrt.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            cliPrintHelp();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            cliPrintVersion();
            return EXIT_SUCCESS;
        }
    }
    VKRT* vkrt = NULL;
    Session session = {0};
    RenderSequencer renderSequencer = {0};
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

    VKRT_CreateInfo createInfo = {0};
    VKRT_defaultCreateInfo(&createInfo);
    createInfo.title = "vkrt";

    if (VKRT_initWithCreateInfo(vkrt, &createInfo) != VKRT_SUCCESS) {
        LOG_ERROR("Failed to initialize VKRT runtime");
        VKRT_deinit(vkrt);
        VKRT_destroy(vkrt);
        sessionDeinit(&session);
        return EXIT_FAILURE;
    }

    meshControllerLoadDefaultAssets(vkrt, &session);

    while (!VKRT_shouldDeinit(vkrt)) {
        VKRT_poll(vkrt);
        editorUIProcessDialogs(&session);
        meshControllerApplySessionActions(vkrt, &session);
        renderSequencerHandleCommands(&renderSequencer, vkrt, &session);
        editorUIUpdate(vkrt, &session);

        if (VKRT_draw(vkrt) != VKRT_SUCCESS) {
            LOG_ERROR("Frame render failed");
            exitCode = EXIT_FAILURE;
            break;
        }
        renderSequencerUpdate(&renderSequencer, vkrt, &session);

        char* savePath = NULL;
        if (sessionTakeRenderSave(&session, &savePath)) {
            if (VKRT_saveRenderPNG(vkrt, savePath) != VKRT_SUCCESS) {
                LOG_ERROR("Failed to queue render PNG save: %s", savePath);
            }
            free(savePath);
        }
    }

    VKRT_deinit(vkrt);
    VKRT_destroy(vkrt);
    sessionDeinit(&session);
    return exitCode;
}
