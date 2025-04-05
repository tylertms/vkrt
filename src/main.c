#include "vkrt.h"

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

    return 0;
}
