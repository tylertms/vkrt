#include "vkrt.h"
#include "gui.h"
#include "object.h"

#include <stdio.h>
#include <stdlib.h>

int main() {
    VKRT vkrt = {0};

    VKRT_AppHooks hooks = {
        .init = initGUI,
        .deinit = deinitGUI,
        .drawOverlay = drawGUI,
        .userData = NULL,
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
        return EXIT_FAILURE;
    }

    loadObject(&vkrt, "assets/models/sphere.glb");
    loadObject(&vkrt, "assets/models/dragon.glb");

    printf("Controls:\n");
    printf("  [1] Add sphere mesh\n");
    printf("  [2] Add dragon mesh\n");
    printf("  [Backspace] Remove last mesh\n");

    int addSphereWasDown = 0;
    int addDragonWasDown = 0;
    int removeWasDown = 0;

    while (!VKRT_shouldDeinit(&vkrt)) {
        VKRT_poll(&vkrt);

        int addSphereDown = glfwGetKey(vkrt.runtime.window, GLFW_KEY_1) == GLFW_PRESS;
        int addDragonDown = glfwGetKey(vkrt.runtime.window, GLFW_KEY_2) == GLFW_PRESS;
        int removeDown = glfwGetKey(vkrt.runtime.window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;

        if (addSphereDown && !addSphereWasDown) {
            loadObject(&vkrt, "assets/models/sphere.glb");
        }

        if (addDragonDown && !addDragonWasDown) {
            loadObject(&vkrt, "assets/models/dragon.glb");
        }

        if (removeDown && !removeWasDown) {
            uint32_t meshCount = VKRT_getMeshCount(&vkrt);
            if (meshCount > 0) {
                VKRT_removeMesh(&vkrt, meshCount - 1);
            }
        }

        addSphereWasDown = addSphereDown;
        addDragonWasDown = addDragonDown;
        removeWasDown = removeDown;

        VKRT_beginFrame(&vkrt);
        VKRT_updateScene(&vkrt);
        VKRT_trace(&vkrt);
        VKRT_present(&vkrt);
        VKRT_endFrame(&vkrt);
    }

    VKRT_deinit(&vkrt);

    return EXIT_SUCCESS;
}
