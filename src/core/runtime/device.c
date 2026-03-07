#include "device.h"
#include "swapchain.h"
#include "validation.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

const char* deviceExtensions[NUM_EXTENSIONS] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_RAY_QUERY_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
};

const VkPhysicalDeviceType rankedDeviceTypes[4] = {
    VK_PHYSICAL_DEVICE_TYPE_CPU,
    VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
    VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
    VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
};

VKRT_Result pickPhysicalDevice(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    vkrt->core.physicalDevice = VK_NULL_HANDLE;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vkrt->core.instance, &deviceCount, NULL);

    if (deviceCount == 0) {
        LOG_ERROR("Failed to find a GPU with Vulkan support");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkPhysicalDevice* devices = (VkPhysicalDevice*)malloc(deviceCount * sizeof(VkPhysicalDevice));
    if (!devices) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
    vkEnumeratePhysicalDevices(vkrt->core.instance, &deviceCount, devices);

    int32_t highestScore = -1;
    int32_t bestDevice = -1;

    LOG_INFO("Found %u Vulkan device(s):", deviceCount);
    for (uint32_t i = 0; i < deviceCount; i++) {
        VkPhysicalDeviceProperties props = {0};
        vkGetPhysicalDeviceProperties(devices[i], &props);

        vkrt->core.physicalDevice = devices[i];
        int32_t score = isDeviceSuitable(vkrt);

        const char* typeName = "Unknown";
        switch (props.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   typeName = "Discrete GPU";   break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: typeName = "Integrated GPU"; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    typeName = "Virtual GPU";    break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:            typeName = "CPU";            break;
            default: break;
        }
        LOG_INFO("  [%u] %s (%s)%s", i, props.deviceName, typeName, score < 0 ? " - not suitable" : "");

        if (score > highestScore) {
            highestScore = score;
            bestDevice = i;
        }
    }

    if (bestDevice < 0) {
        LOG_ERROR("Failed to find a suitable GPU");
        free(devices);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->core.physicalDevice = devices[bestDevice];

    VkPhysicalDeviceProperties deviceProperties = {0};
    vkGetPhysicalDeviceProperties(vkrt->core.physicalDevice, &deviceProperties);

    LOG_INFO("Selected device [%u]: %s", bestDevice, deviceProperties.deviceName);
    snprintf(vkrt->core.deviceName, VKRT_ARRAY_COUNT(vkrt->core.deviceName), "%s", deviceProperties.deviceName);
    vkrt->core.vendorID = deviceProperties.vendorID;
    vkrt->core.driverVersion = deviceProperties.driverVersion;
    free(devices);
    return VKRT_SUCCESS;
}

VKRT_Result createLogicalDevice(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    QueueFamily indices = findQueueFamilies(vkrt);
    if (!isQueueFamilyComplete(indices)) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
    vkrt->core.indices = indices;

    float queuePriority = 1.0f;
    uint32_t uniqueQueueFamilies[2] = {indices.graphics, indices.present};
    uint32_t uniqueQueueFamilyCount = 2;

    VkDeviceQueueCreateInfo* queueCreateInfos = (VkDeviceQueueCreateInfo*)malloc(uniqueQueueFamilyCount * sizeof(VkDeviceQueueCreateInfo));
    if (!queueCreateInfos) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
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

    VkPhysicalDeviceBufferDeviceAddressFeatures deviceBufferDeviceAddressFeatures = {0};
    deviceBufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
    deviceBufferDeviceAddressFeatures.pNext = NULL;
    deviceBufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR deviceAccelerationStructureFeatures = {0};
    deviceAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    deviceAccelerationStructureFeatures.pNext = &deviceBufferDeviceAddressFeatures;
    deviceAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
    deviceAccelerationStructureFeatures.accelerationStructureCaptureReplay = VK_FALSE;
    deviceAccelerationStructureFeatures.accelerationStructureIndirectBuild = VK_FALSE;
    deviceAccelerationStructureFeatures.accelerationStructureHostCommands = VK_FALSE;
    deviceAccelerationStructureFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_FALSE;

    VkPhysicalDeviceRayQueryFeaturesKHR deviceRayQueryFeatures = {0};
    deviceRayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    deviceRayQueryFeatures.pNext = &deviceAccelerationStructureFeatures;
    deviceRayQueryFeatures.rayQuery = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR deviceRayTracingPipelineFeatures = {0};
    deviceRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    deviceRayTracingPipelineFeatures.pNext = &deviceRayQueryFeatures;
    deviceRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
    deviceRayTracingPipelineFeatures.rayTracingPipelineShaderGroupHandleCaptureReplay = VK_FALSE;
    deviceRayTracingPipelineFeatures.rayTracingPipelineShaderGroupHandleCaptureReplayMixed = VK_FALSE;
    deviceRayTracingPipelineFeatures.rayTracingPipelineTraceRaysIndirect = VK_FALSE;
    deviceRayTracingPipelineFeatures.rayTraversalPrimitiveCulling = VK_FALSE;

    VkPhysicalDeviceFeatures deviceFeatures = {0};

    VkDeviceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = queueCreateInfos;
    createInfo.queueCreateInfoCount = queueCreateInfoCount;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.pNext = &deviceRayTracingPipelineFeatures;
    createInfo.enabledExtensionCount = NUM_EXTENSIONS;
    createInfo.ppEnabledExtensionNames = deviceExtensions;

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = numValidationLayers;
        createInfo.ppEnabledLayerNames = validationLayers;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(vkrt->core.physicalDevice, &createInfo, NULL, &vkrt->core.device) != VK_SUCCESS) {
        LOG_ERROR("Failed to create logical device");
        free(queueCreateInfos);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkGetDeviceQueue(vkrt->core.device, indices.graphics, 0, &vkrt->core.graphicsQueue);
    vkGetDeviceQueue(vkrt->core.device, indices.present, 0, &vkrt->core.presentQueue);

    free(queueCreateInfos);
    return VKRT_SUCCESS;
}

VKRT_Result createQueryPool(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    VkQueryPoolCreateInfo qp = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = VKRT_MAX_FRAMES_IN_FLIGHT * 2
    };

    if (vkCreateQueryPool(vkrt->core.device, &qp, NULL, &vkrt->runtime.timestampPool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create timestamp query pool");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkPhysicalDeviceProperties deviceProperties = {0};
    vkGetPhysicalDeviceProperties(vkrt->core.physicalDevice, &deviceProperties);
    vkrt->runtime.timestampPeriod = deviceProperties.limits.timestampPeriod;
    return VKRT_SUCCESS;
}

QueueFamily findQueueFamilies(VKRT* vkrt) {
    QueueFamily indices;
    indices.graphics = -1;
    indices.present = -1;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vkrt->core.physicalDevice, &queueFamilyCount, NULL);

    VkQueueFamilyProperties* queueFamilies = (VkQueueFamilyProperties*)malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    if (!queueFamilies) {
        return indices;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(vkrt->core.physicalDevice, &queueFamilyCount, queueFamilies);

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(vkrt->core.physicalDevice, i, vkrt->runtime.surface, &presentSupport);

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

int32_t isDeviceSuitable(VKRT* vkrt) {
    VkPhysicalDeviceProperties deviceProperties = {0};
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {0};
    bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {0};
    accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelerationStructureFeatures.pNext = &bufferDeviceAddressFeatures;

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {0};
    rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayQueryFeatures.pNext = &accelerationStructureFeatures;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures = {0};
    rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rayTracingPipelineFeatures.pNext = &rayQueryFeatures;

    VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {0};
    physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    physicalDeviceFeatures.pNext = &rayTracingPipelineFeatures;

    vkGetPhysicalDeviceProperties(vkrt->core.physicalDevice, &deviceProperties);
    vkGetPhysicalDeviceFeatures2(vkrt->core.physicalDevice, &physicalDeviceFeatures);

    QueueFamily indices = findQueueFamilies(vkrt);
    VkBool32 queueFamilyComplete = isQueueFamilyComplete(indices);

    VkBool32 extensionSupport = extensionsSupported(vkrt->core.physicalDevice);

    VkBool32 swapChainAdequate = VK_FALSE;
    if (extensionSupport) {
        SwapChainSupportDetails supportDetails = {0};
        if (querySwapChainSupport(vkrt, &supportDetails) == VKRT_SUCCESS) {
            swapChainAdequate = supportDetails.formatCount && supportDetails.presentModeCount;
        }

        free(supportDetails.formats);
        free(supportDetails.presentModes);
    }

    VkBool32 requiredFeatures = bufferDeviceAddressFeatures.bufferDeviceAddress &&
                                accelerationStructureFeatures.accelerationStructure &&
                                rayQueryFeatures.rayQuery &&
                                rayTracingPipelineFeatures.rayTracingPipeline;

    if (queueFamilyComplete && extensionSupport && swapChainAdequate && requiredFeatures) {
        for (uint32_t i = 0; i < VKRT_ARRAY_COUNT(rankedDeviceTypes); i++) {
            if (deviceProperties.deviceType == rankedDeviceTypes[i]) {
                return i;
            }
        }
    }

    return -1;
}

VkBool32 isQueueFamilyComplete(QueueFamily indices) {
    return indices.graphics >= 0 && indices.present >= 0;
}

VkBool32 extensionsSupported(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);

    VkExtensionProperties* availableExtensions = (VkExtensionProperties*)malloc(extensionCount * sizeof(VkExtensionProperties));
    if (!availableExtensions) return VK_FALSE;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, availableExtensions);

    for (uint32_t i = 0; i < NUM_EXTENSIONS; i++) {
        VkBool32 extensionAvailable = VK_FALSE;
        for (uint32_t j = 0; j < extensionCount; j++) {
            if (!strcmp(deviceExtensions[i], availableExtensions[j].extensionName)) {
                extensionAvailable = VK_TRUE;
            }
        }

        if (!extensionAvailable) {
            free(availableExtensions);
            return VK_FALSE;
        }
    }

    free(availableExtensions);
    return VK_TRUE;
}

VKRT_Result findMemoryType(VKRT* vkrt, uint32_t typeFilter, VkMemoryPropertyFlags properties, uint32_t* outMemoryTypeIndex) {
    if (!vkrt || !outMemoryTypeIndex) return VKRT_ERROR_INVALID_ARGUMENT;

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(vkrt->core.physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            *outMemoryTypeIndex = i;
            return VKRT_SUCCESS;
        }
    }

    LOG_ERROR("Failed to find suitable memory type");
    return VKRT_ERROR_OPERATION_FAILED;
}
