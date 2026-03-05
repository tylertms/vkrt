#include "instance.h"
#include "validation.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>

VKRT_Result createInstance(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (enableValidationLayers && !checkValidationLayerSupport()) {
        LOG_ERROR("Validation layers requested but not available");
        return VKRT_ERROR_OPERATION_FAILED;
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
    if (!extensions) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

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

    if (vkCreateInstance(&instanceCreateInfo, 0, &vkrt->core.instance) != VK_SUCCESS) {
        LOG_ERROR("Failed to create instance");
        free(extensions);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    free(extensions);
    return VKRT_SUCCESS;
}
