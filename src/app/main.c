#include "session.h"
#include "render/sequencer.h"
#include "editor/editor.h"
#include "controller.h"
#include "vkrt.h"
#include "debug.h"

#include <stdlib.h>

int main(void) {
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
    createInfo.shaders.rgenPath = "./shaders/rgen.spv";
    createInfo.shaders.rmissPath = "./shaders/rmiss.spv";
    createInfo.shaders.rchitPath = "./shaders/rchit.spv";

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
