#include "validation.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* validationLayers[] = {
    "VK_LAYER_KHRONOS_validation"
};

const uint32_t numValidationLayers = 1;

#if VKRT_VALIDATION_ENABLED
const VkBool32 enableValidationLayers = 1;
#else
const VkBool32 enableValidationLayers = 0;
#endif

#if VKRT_DEBUG_UTILS_ENABLED
const VkBool32 enableDebugUtils = 1;
#else
const VkBool32 enableDebugUtils = 0;
#endif

int checkValidationLayerSupport(void) {
    uint32_t layerCount = 0;
    if (vkEnumerateInstanceLayerProperties(&layerCount, NULL) != VK_SUCCESS) {
        return VK_FALSE;
    }

    VkLayerProperties* availableLayers = (VkLayerProperties*)malloc(layerCount * sizeof(VkLayerProperties));
    if (!availableLayers) return VK_FALSE;
    if (vkEnumerateInstanceLayerProperties(&layerCount, availableLayers) != VK_SUCCESS) {
        free(availableLayers);
        return VK_FALSE;
    }

    for (uint32_t i = 0; i < numValidationLayers; i++) {
        int layerFound = 0;

        for (uint32_t j = 0; j < layerCount; j++) {
            if (!strcmp(validationLayers[i], availableLayers[j].layerName)) {
                layerFound = 1;
                break;
            }
        }

        if (!layerFound) {
            free(availableLayers);
            return VK_FALSE;
        }
    }

    free(availableLayers);
    return VK_TRUE;
}

VKRT_Result getRequiredExtensions(uint32_t* extensionCount, VkBool32 requirePresentation, const char*** outExtensions) {
    if (!extensionCount || !outExtensions) return VKRT_ERROR_INVALID_ARGUMENT;

    *extensionCount = 0;
    *outExtensions = NULL;

    const char** baseExtensions = NULL;
    uint32_t baseCount = 0;
    if (requirePresentation) {
        baseExtensions = glfwGetRequiredInstanceExtensions(&baseCount);
        if (!baseExtensions || baseCount == 0u) {
            LOG_ERROR("GLFW did not provide the required Vulkan instance extensions");
            return VKRT_ERROR_INITIALIZATION_FAILED;
        }
    }

    uint32_t count = baseCount;

    if (enableDebugUtils) {
        count++;
    }

    if (count == 0) {
        return VKRT_SUCCESS;
    }

    const char** extensions = (const char**)malloc(sizeof(const char*) * count);
    if (!extensions) {
        LOG_ERROR("Failed to allocate Vulkan instance extension list");
        return VKRT_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < baseCount; ++i) {
        extensions[i] = baseExtensions[i];
    }

    if (enableDebugUtils) {
        extensions[baseCount] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }

    *extensionCount = count;
    *outExtensions = extensions;
    return VKRT_SUCCESS;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
) {

    (void)pUserData;
    printf("%s - %s: %s\n", severityString(messageSeverity), typeString(messageType), pCallbackData->pMessage);

    return VK_FALSE;
}

void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo) {
    createInfo->sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo->messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo->messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo->pfnUserCallback = debugCallback;
}

VKRT_Result setupDebugMessenger(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (!enableValidationLayers)
        return VKRT_SUCCESS;

    VkDebugUtilsMessengerCreateInfoEXT createInfo = {0};
    populateDebugMessengerCreateInfo(&createInfo);

    if (createDebugUtilsMessengerEXT(vkrt->core.instance, &createInfo, 0, &vkrt->core.debugMessenger) != VK_SUCCESS) {
        LOG_ERROR("Failed to set up debug messenger");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    return VKRT_SUCCESS;
}

VkResult createDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != 0) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void destroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != 0) {
        func(instance, debugMessenger, pAllocator);
    }
}

const char* severityString(VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
    switch (severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        return "[VERBOSE]";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        return "[INFO]";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        return "[ERROR]";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        return "[WARNING]";
    default:
        return "[OTHER]";
    }
}

const char* typeString(VkDebugUtilsMessageTypeFlagsEXT type) {
    switch (type) {
    case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
        return "GENERAL";
    case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
        return "VALIDATION";
    case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
        return "PERFORMANCE";
    case VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT:
        return "DEVICE ADDRESS BINDING";
    default:
        return "OTHER";
    }
}
