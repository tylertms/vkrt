#include "validation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* validationLayers[] = {
    "VK_LAYER_KHRONOS_validation"};

const uint32_t numValidationLayers = 1;

#ifdef NODEBUG
const VkBool32 enableValidationLayers = 0;
#else
const VkBool32 enableValidationLayers = 1;
#endif

int checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, 0);

    VkLayerProperties* availableLayers = (VkLayerProperties*)malloc(layerCount * sizeof(VkLayerProperties));
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

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

const char** getRequiredExtensions(uint32_t* extensionCount) {
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(extensionCount);

    uint32_t count = *extensionCount;

    if (enableValidationLayers) {
        count++;
    }

    const char** extensions = (const char**)malloc(sizeof(const char*) * count);

    for (uint32_t i = 0; i < *extensionCount; ++i) {
        extensions[i] = glfwExtensions[i];
    }

    if (enableValidationLayers) {
        extensions[*extensionCount] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }

    *extensionCount = count;
    return extensions;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

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

void setupDebugMessenger(VKRT* vkrt) {
    if (!enableValidationLayers)
        return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo = {0};
    populateDebugMessengerCreateInfo(&createInfo);

    if (CreateDebugUtilsMessengerEXT(vkrt->instance, &createInfo, 0, &vkrt->debugMessenger) != VK_SUCCESS) {
        perror("ERROR: Failed to set up debug messenger");
        exit(EXIT_FAILURE);
    }
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != 0) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != 0) {
        func(instance, debugMessenger, pAllocator);
    }
}

const char* severityString(VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
    switch (severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        return "VERBOSE";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        return "INFO";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        return "ERROR";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        return "WARNING";
    default:
        return "OTHER";
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