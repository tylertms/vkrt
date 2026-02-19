#include "vkrt.h"
#include "gui.h"
#include "demo_actions.h"

#include <stdio.h>
#include <stdlib.h>

int main() {
    VKRT vkrt = {0};
    DemoGUIState guiState = {0};
    initDemoGUIState(&guiState);

    VKRT_AppHooks hooks = {
        .init = initGUI,
        .deinit = deinitGUI,
        .drawOverlay = drawGUI,
        .userData = &guiState,
    };
    VKRT_registerAppHooks(&vkrt, hooks);

    VKRT_CreateInfo createInfo = {0};
    VKRT_defaultCreateInfo(&createInfo);
    createInfo.title = "VKRT Demo";
    createInfo.shaders.rgenPath = "./rgen.spv";
    createInfo.shaders.rmissPath = "./rmiss.spv";
    createInfo.shaders.rchitPath = "./rchit.spv";

    if (VKRT_initWithCreateInfo(&vkrt, &createInfo) != VK_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to initialize VKRT\n");
        VKRT_deinit(&vkrt);
        deinitDemoGUIState(&guiState);
        return EXIT_FAILURE;
    }

    demoLoadInitialMeshes(&vkrt, &guiState);

    while (!VKRT_shouldDeinit(&vkrt)) {
        VKRT_poll(&vkrt);
        demoProcessPendingActions(&vkrt, &guiState);

        VKRT_beginFrame(&vkrt);
        VKRT_updateScene(&vkrt);
        VKRT_trace(&vkrt);
        VKRT_present(&vkrt);
        VKRT_endFrame(&vkrt);
    }

    VKRT_deinit(&vkrt);
    deinitDemoGUIState(&guiState);

    return EXIT_SUCCESS;
}
