#include "instance.h"
#include "validation.h"
#include <stdio.h>
#include <stdlib.h>

void createInstance(VKRT* vkrt) {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        printf("ERROR: Validation layers requested but not available!");
        exit(EXIT_FAILURE);
    }

    VkApplicationInfo appInfo = {0};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VKRT";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;
    appInfo.pNext = 0;

    VkInstanceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    uint32_t extensionCount;
    const char** extensions = getRequiredExtensions(&extensionCount);

    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {0};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = numValidationLayers;
        createInfo.ppEnabledLayerNames = validationLayers;

        populateDebugMessengerCreateInfo(&debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = 0;
    }

    if (vkCreateInstance(&createInfo, 0, &vkrt->instance) != VK_SUCCESS) {
        printf("ERROR: Failed to create instance!\n");
        free(extensions);
        exit(EXIT_FAILURE);
    }

    free(extensions);
    return;
}