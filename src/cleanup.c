#include "cleanup.h"
#include "validation.h"

void cleanup(VKRT* vkrt) {
    vkDestroySwapchainKHR(vkrt->device, vkrt->swapChain, NULL);

    vkDestroyDevice(vkrt->device, NULL);

    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(vkrt->instance, vkrt->debugMessenger, NULL);
    }

    vkDestroySurfaceKHR(vkrt->instance, vkrt->surface, NULL);
    vkDestroyInstance(vkrt->instance, NULL);

    glfwDestroyWindow(vkrt->window);

    glfwTerminate();
}