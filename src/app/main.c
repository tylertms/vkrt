#include "state.h"
#include "editor.h"
#include "controller.h"
#include "vkrt.h"

#include <stdio.h>
#include <stdlib.h>

static void runFrame(VKRT* runtime, State* state) {
    VKRT_poll(runtime);
    sceneControllerApplyPendingActions(runtime, state);

    VKRT_beginFrame(runtime);
    VKRT_updateScene(runtime);
    VKRT_trace(runtime);
    VKRT_present(runtime);
    VKRT_endFrame(runtime);
}

int main(void) {
    VKRT runtime = {0};
    State state = {0};
    stateInit(&state);

    VKRT_AppHooks hooks = {
        .init = editorUIInitialize,
        .deinit = editorUIShutdown,
        .drawOverlay = editorUIDraw,
        .userData = &state,
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
        stateDeinit(&state);
        return EXIT_FAILURE;
    }

    sceneControllerLoadDefaultAssets(&runtime, &state);

    while (!VKRT_shouldDeinit(&runtime)) {
        runFrame(&runtime, &state);
    }

    VKRT_deinit(&runtime);
    stateDeinit(&state);
    return EXIT_SUCCESS;
}
