#include "device.h"
#include "validation.h"
#include "swapchain.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

const char* deviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME
};

const uint32_t numDeviceExtensions = 3;

void pickPhysicalDevice(VKRT* vkrt) {
    vkrt->physicalDevice = VK_NULL_HANDLE;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vkrt->instance, &deviceCount, NULL);

    if (deviceCount == 0) {
        printf("ERROR: Failed to find a GPU with Vulkan support!\n");
        exit(EXIT_FAILURE);
    }

    VkPhysicalDevice* devices = (VkPhysicalDevice*)malloc(deviceCount * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(vkrt->instance, &deviceCount, devices);

    for (int i = 0; i < deviceCount; i++) {
        vkrt->physicalDevice = devices[i];
        if (isDeviceSuitable(vkrt)) {
            break;
        }
        vkrt->physicalDevice = VK_NULL_HANDLE;
    }

    free(devices);

    if (vkrt->physicalDevice == VK_NULL_HANDLE) {
        printf("ERROR: Failed to find a suitable GPU!\n");
        exit(EXIT_FAILURE);
    }
}

void createLogicalDevice(VKRT* vkrt) {
    QueueFamily indices = findQueueFamilies(vkrt);

    float queuePriority = 1.0f;

    uint32_t uniqueQueueFamilies[2] = { indices.graphics, indices.present };
    uint32_t uniqueQueueFamilyCount = 2;

    VkDeviceQueueCreateInfo* queueCreateInfos = malloc(uniqueQueueFamilyCount * sizeof(VkDeviceQueueCreateInfo));
    uint32_t queueCreateInfoCount = 0;

    for (uint32_t i = 0; i < uniqueQueueFamilyCount; i++) {
        VkBool32 duplicate = VK_FALSE;
        for (uint32_t j = 0; j < queueCreateInfoCount; j++) {
            if (uniqueQueueFamilies[i] == queueCreateInfos[j].queueFamilyIndex) {
                duplicate = VK_TRUE;
                break;
            }
        }
        if (!duplicate) {
            VkDeviceQueueCreateInfo queueCreateInfo = {0};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = uniqueQueueFamilies[i];
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos[queueCreateInfoCount] = queueCreateInfo;
            queueCreateInfoCount++;
        }
    }

    VkPhysicalDeviceFeatures deviceFeatures = {0};

    VkDeviceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.pQueueCreateInfos = queueCreateInfos;
    createInfo.queueCreateInfoCount = queueCreateInfoCount;

    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = numDeviceExtensions;
    createInfo.ppEnabledExtensionNames = deviceExtensions;

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = numValidationLayers;
        createInfo.ppEnabledLayerNames = validationLayers;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(vkrt->physicalDevice, &createInfo, NULL, &vkrt->device) != VK_SUCCESS) {
        printf("ERROR: Failed to create logical device!\n");
        exit(EXIT_FAILURE);
    }

    vkGetDeviceQueue(vkrt->device, indices.graphics, 0, &vkrt->graphicsQueue);
    vkGetDeviceQueue(vkrt->device, indices.present, 0, &vkrt->presentQueue);

    free(queueCreateInfos);
}


QueueFamily findQueueFamilies(VKRT* vkrt) {
    QueueFamily indices;
    indices.graphics = -1;
    indices.present = -1;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vkrt->physicalDevice, &queueFamilyCount, NULL);

    VkQueueFamilyProperties* queueFamilies = (VkQueueFamilyProperties*)malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(vkrt->physicalDevice, &queueFamilyCount, queueFamilies);

    for (int i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(vkrt->physicalDevice, i, vkrt->surface, &presentSupport);

        if (presentSupport) {
            indices.present = i;
        }

        if (isQueueFamilyComplete(indices)) {
            break;
        }
    }

    free(queueFamilies);

    return indices;
}

VkBool32 isDeviceSuitable(VKRT* vkrt) {
    VkPhysicalDeviceProperties deviceProperties = {0};
    VkPhysicalDeviceFeatures deviceFeatures = {0};
    vkGetPhysicalDeviceProperties(vkrt->physicalDevice, &deviceProperties);
    vkGetPhysicalDeviceFeatures(vkrt->physicalDevice, &deviceFeatures);

    QueueFamily indices = findQueueFamilies(vkrt);
    VkBool32 queueFamilyComplete = isQueueFamilyComplete(indices);

    VkBool32 extensionSupport = extensionsSupported(vkrt->physicalDevice);

    VkBool32 swapChainAdequate = VK_FALSE;
    if (extensionSupport) {
        SwapChainSupportDetails supportDetails = querySwapChainSupport(vkrt);
        swapChainAdequate = supportDetails.formatCount && supportDetails.presentModeCount;

        free(supportDetails.formats);
        free(supportDetails.presentModes);
    }

    VkBool32 validDeviceType = deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

    if (queueFamilyComplete && extensionSupport && swapChainAdequate && validDeviceType) {
        printf("INFO: Using device [%s].\n", deviceProperties.deviceName);
        return VK_TRUE;
    }

    return VK_FALSE;
}

VkBool32 isQueueFamilyComplete(QueueFamily indices) {
    return indices.graphics >= 0 && indices.present >= 0;
}

VkBool32 extensionsSupported(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);

    VkExtensionProperties* availableExtensions = (VkExtensionProperties*)malloc(extensionCount * sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, availableExtensions);

    for (uint32_t i = 0; i < numDeviceExtensions; i++) {
        VkBool32 extensionAvailable = VK_FALSE;
        for (uint32_t j = 0; j < extensionCount; j++) {
            if (!strcmp(deviceExtensions[i], availableExtensions[j].extensionName)) {
                extensionAvailable = VK_TRUE;
            }
        }

        if (!extensionAvailable) {
            return VK_FALSE;
        }
    }

    return VK_TRUE;
}