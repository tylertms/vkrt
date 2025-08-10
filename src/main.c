#include "app.h"
#include "vkrt.h"

#include <stdlib.h>

int main() {
    VKRT vkrt = {0};

    if (VKRT_init(&vkrt) != VK_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to initialize VKRT\n");
        VKRT_deinit(&vkrt);
        return EXIT_FAILURE;
    }

    VKRT_addMesh(&vkrt, "assets/models/sphere.glb");
    VKRT_addMesh(&vkrt, "assets/models/dragon.glb");

    while (!VKRT_shouldDeinit(&vkrt)) {
        VKRT_poll(&vkrt);
        VKRT_draw(&vkrt);
    }

    VKRT_deinit(&vkrt);
    
    return EXIT_SUCCESS;
}
