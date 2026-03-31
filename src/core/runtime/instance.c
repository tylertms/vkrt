#include "instance.h"

#include "debug.h"
#include "validation.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdint.h>
#include <stdlib.h>

VKRT_Result createInstance(VKRT* vkrt, VkBool32 requirePresentation) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (enableValidationLayers && !checkValidationLayerSupport()) {
        LOG_ERROR("Validation layers requested but not available");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint32_t loaderApiVersion = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion enumerateInstanceVersion =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (enumerateInstanceVersion && enumerateInstanceVersion(&loaderApiVersion) != VK_SUCCESS) {
        loaderApiVersion = VK_API_VERSION_1_0;
    }

    const uint32_t requestedApiVersion = VK_API_VERSION_1_3;
    if (loaderApiVersion < requestedApiVersion) {
        LOG_ERROR(
            "Vulkan loader version %u.%u.%u is below the required %u.%u.%u",
            VK_API_VERSION_MAJOR(loaderApiVersion),
            VK_API_VERSION_MINOR(loaderApiVersion),
            VK_API_VERSION_PATCH(loaderApiVersion),
            VK_API_VERSION_MAJOR(requestedApiVersion),
            VK_API_VERSION_MINOR(requestedApiVersion),
            VK_API_VERSION_PATCH(requestedApiVersion)
        );
        return VKRT_ERROR_INITIALIZATION_FAILED;
    }

    VkApplicationInfo applicationInfo = {0};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "VKRT";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.pEngineName = "No Engine";
    applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.apiVersion = requestedApiVersion;
    applicationInfo.pNext = 0;

    VkInstanceCreateInfo instanceCreateInfo = {0};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &applicationInfo;

    uint32_t extensionCount = 0;
    const char** extensions = NULL;
    VKRT_Result extensionResult = getRequiredExtensions(&extensionCount, requirePresentation, &extensions);
    if (extensionResult != VKRT_SUCCESS) {
        return extensionResult;
    }

    instanceCreateInfo.enabledExtensionCount = extensionCount;
    instanceCreateInfo.ppEnabledExtensionNames = extensionCount > 0u ? extensions : NULL;

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {0};
    if (enableValidationLayers) {
        instanceCreateInfo.enabledLayerCount = numValidationLayers;
        instanceCreateInfo.ppEnabledLayerNames = validationLayers;

        populateDebugMessengerCreateInfo(&debugCreateInfo);
        instanceCreateInfo.pNext = (&debugCreateInfo);
    } else {
        instanceCreateInfo.enabledLayerCount = 0;
        instanceCreateInfo.pNext = 0;
    }

    if (vkCreateInstance(&instanceCreateInfo, 0, &vkrt->core.instance) != VK_SUCCESS) {
        LOG_ERROR("Failed to create instance");
        free((void*)extensions);
        return VKRT_ERROR_INITIALIZATION_FAILED;
    }

    free((void*)extensions);
    vkrt->core.apiVersion = requestedApiVersion;
    return VKRT_SUCCESS;
}
