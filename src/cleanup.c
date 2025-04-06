#include "cleanup.h"
#include "validation.h"
#include <stdlib.h>

void cleanup(VKRT* vkrt) {
    for (size_t i = 0; i < vkrt->swapChainImageCount; i++) {
        vkDestroyImageView(vkrt->device, vkrt->swapChainImageViews[i], NULL);
    }

    free(vkrt->swapChainImageViews);
    free(vkrt->swapChainImages);

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