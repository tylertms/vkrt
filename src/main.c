#include "vkrt.h"
#include "gui.h"
#include <stdlib.h>

int main() {
    VKRT vkrt = {0};

    // Register GUI callbacks, more information at each function
    VKRT_registerGUI(&vkrt, initGUI, deinitGUI, drawGUI);

    // Initialize all parts of the GLFW window and Vulkan
    if (VKRT_init(&vkrt) != VK_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to initialize VKRT\n");
        VKRT_deinit(&vkrt);
        return EXIT_FAILURE;
    }

    // Add meshes - BLAS/TLAS auto-updated
    VKRT_addMesh(&vkrt, "assets/models/sphere.glb");
    VKRT_addMesh(&vkrt, "assets/models/dragon.glb");

    // Main loop, waiting for window to close
    while (!VKRT_shouldDeinit(&vkrt)) {
        VKRT_poll(&vkrt);

        // Draw with the Ray Tracing Pipeline
        // Also calls the drawGUI callback on
        // a secondary GUI render pass if present
        VKRT_draw(&vkrt);
    }

    // Cleanup all Vulkan and GLFW window resources
    VKRT_deinit(&vkrt);

    return EXIT_SUCCESS;
}