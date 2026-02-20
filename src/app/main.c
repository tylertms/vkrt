#include "state.h"
#include "editor.h"
#include "controller.h"
#include "vkrt.h"

#include <stdio.h>
#include <stdlib.h>

static void runFrame(VKRT* runtime, EditorState* editorState) {
    VKRT_poll(runtime);
    sceneControllerApplyPendingActions(runtime, editorState);

    VKRT_beginFrame(runtime);
    VKRT_updateScene(runtime);
    VKRT_trace(runtime);
    VKRT_present(runtime);
    VKRT_endFrame(runtime);
}

int main(void) {
    VKRT runtime = {0};
    EditorState editorState = {0};
    editorStateInit(&editorState);

    VKRT_AppHooks hooks = {
        .init = editorUIInitialize,
        .deinit = editorUIShutdown,
        .drawOverlay = editorUIDraw,
        .userData = &editorState,
    };
    VKRT_registerAppHooks(&runtime, hooks);

    VKRT_CreateInfo createInfo = {0};
    VKRT_defaultCreateInfo(&createInfo);
    createInfo.title = "vkspt";
    createInfo.shaders.rgenPath = "./shaders/rgen.spv";
    createInfo.shaders.rmissPath = "./shaders/rmiss.spv";
    createInfo.shaders.rchitPath = "./shaders/rchit.spv";

    if (VKRT_initWithCreateInfo(&runtime, &createInfo) != VK_SUCCESS) {
        fprintf(stderr, "[ERROR]: Failed to initialize VKRT runtime\n");
        VKRT_deinit(&runtime);
        editorStateDeinit(&editorState);
        return EXIT_FAILURE;
    }

    sceneControllerLoadDefaultAssets(&runtime, &editorState);

    while (!VKRT_shouldDeinit(&runtime)) {
        runFrame(&runtime, &editorState);
    }

    VKRT_deinit(&runtime);
    editorStateDeinit(&editorState);
    return EXIT_SUCCESS;
}
