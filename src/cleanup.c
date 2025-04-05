#include "cleanup.h"

void cleanup(VKRT* vkrt) {
    vkDestroyInstance(vkrt->instance, 0);

    glfwDestroyWindow(vkrt->window);

    glfwTerminate();
}