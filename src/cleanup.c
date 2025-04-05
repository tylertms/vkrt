#include "cleanup.h"
#include "validation.h"

void cleanup(VKRT* vkrt) {
    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(vkrt->instance, vkrt->debugMessenger, 0);
    }

    vkDestroyInstance(vkrt->instance, 0);

    glfwDestroyWindow(vkrt->window);

    glfwTerminate();
}