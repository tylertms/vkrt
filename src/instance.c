#include "instance.h"
#include "validation.h"

#include <stdio.h>
#include <stdlib.h>

void createInstance(VKRT* vkrt) {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        perror("ERROR: Validation layers requested but not available");
        exit(EXIT_FAILURE);
    }

    VkApplicationInfo applicationInfo = {0};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "VKRT";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.pEngineName = "No Engine";
    applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_4;
    applicationInfo.pNext = 0;

    VkInstanceCreateInfo instanceCreateInfo = {0};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &applicationInfo;

    uint32_t extensionCount;
    const char** extensions = getRequiredExtensions(&extensionCount);

    instanceCreateInfo.enabledExtensionCount = extensionCount;
    instanceCreateInfo.ppEnabledExtensionNames = extensions;

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {0};
    if (enableValidationLayers) {
        instanceCreateInfo.enabledLayerCount = numValidationLayers;
        instanceCreateInfo.ppEnabledLayerNames = validationLayers;

        populateDebugMessengerCreateInfo(&debugCreateInfo);
        instanceCreateInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    } else {
        instanceCreateInfo.enabledLayerCount = 0;
        instanceCreateInfo.pNext = 0;
    }

    if (vkCreateInstance(&instanceCreateInfo, 0, &vkrt->instance) != VK_SUCCESS) {
        perror("ERROR: Failed to create instance");
        free(extensions);
        exit(EXIT_FAILURE);
    }

    free(extensions);
    return;
}