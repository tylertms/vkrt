#include "cleanup.h"
#include "vkrt.h"
#include "vulkan.h"
#include "window.h"

#include <stdlib.h>

void run(VKRT* vkrt) {
    initWindow(vkrt);
    initVulkan(vkrt);

    while (!glfwWindowShouldClose(vkrt->window)) {
        glfwPollEvents();
    }

    cleanup(vkrt);
}

int main() {
    VKRT vkrt;
    run(&vkrt);

    return EXIT_SUCCESS;
}
