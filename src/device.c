#include "device.h"
#include "validation.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void pickPhysicalDevice(VKRT* vkrt) {
    vkrt->physicalDevice = VK_NULL_HANDLE;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vkrt->instance, &deviceCount, NULL);

    if (deviceCount == 0) {
        printf("ERROR: Failed to find a GPU with Vulkan support!");
        exit(EXIT_FAILURE);
    }

    VkPhysicalDevice* devices = (VkPhysicalDevice*)malloc(deviceCount * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(vkrt->instance, &deviceCount, devices);

    for (int i = 0; i < deviceCount; i++) {
        if (isDeviceSuitable(devices[i])) {
            vkrt->physicalDevice = devices[i];
            break;
        }
    }

    free(devices);

    if (vkrt->physicalDevice == VK_NULL_HANDLE) {
        printf("ERROR: Failed to find a suitable GPU!");
        exit(EXIT_FAILURE);
    }
}

void createLogicalDevice(VKRT* vkrt) {
    QueueFamily indices = findQueueFamilies(vkrt->physicalDevice);

    VkDeviceQueueCreateInfo queueCreateInfo = {0};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = indices.graphics;
    queueCreateInfo.queueCount = 1;

    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures = {0};

    VkDeviceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;

    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = 0;
    createInfo.flags = 0;

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = ARRLEN(validationLayers);
        createInfo.ppEnabledLayerNames = validationLayers;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(vkrt->physicalDevice, &createInfo, NULL, &vkrt->device) != VK_SUCCESS) {
        printf("ERROR: Failed to create logical device!");
        exit(EXIT_FAILURE);
    }

    vkGetDeviceQueue(vkrt->device, indices.graphics, 0, &vkrt->graphicsQueue);
}

QueueFamily findQueueFamilies(VkPhysicalDevice device) {
    QueueFamily indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);

    VkQueueFamilyProperties* queueFamilies = (VkQueueFamilyProperties*)malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

    for (int i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }

        if (isQueueFamilyComplete(indices)) {
            break;
        }
    }

    free(queueFamilies);

    return indices;
}

uint8_t isDeviceSuitable(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties = {0};
    VkPhysicalDeviceFeatures deviceFeatures = {0};
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        if (enableValidationLayers) {
            printf("INFO: Using device [%s].\n", deviceProperties.deviceName);
        }
        return 1;
    }

    return 0;
}

uint8_t isQueueFamilyComplete(QueueFamily indices) {
    return indices.graphics >= 0;
}