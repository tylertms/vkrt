#include "device.h"
#include "swapchain.h"
#include "validation.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const char* deviceExtensions[NUM_EXTENSIONS] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME
};

const VkPhysicalDeviceType rankedDeviceTypes[4] = {
    VK_PHYSICAL_DEVICE_TYPE_CPU,
    VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
    VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
    VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
};

void pickPhysicalDevice(VKRT* vkrt) {
    vkrt->physicalDevice = VK_NULL_HANDLE;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vkrt->instance, &deviceCount, NULL);

    if (deviceCount == 0) {
        perror("ERROR: Failed to find a GPU with Vulkan support");
        exit(EXIT_FAILURE);
    }

    VkPhysicalDevice* devices = (VkPhysicalDevice*)malloc(deviceCount * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(vkrt->instance, &deviceCount, devices);

    int32_t highestScore = -1;
    int32_t bestDevice = -1;

    for (uint32_t i = 0; i < deviceCount; i++) {
        vkrt->physicalDevice = devices[i];
        int32_t score = isDeviceSuitable(vkrt);
        
        if (score > highestScore) {
            highestScore = score;
            bestDevice = i;
            break;
        }
    }

    if (bestDevice < 0) {
        perror("ERROR: Failed to find a suitable GPU");
        free(devices);
        exit(EXIT_FAILURE);
    }

    vkrt->physicalDevice = devices[bestDevice];

    VkPhysicalDeviceProperties deviceProperties = {0};
    vkGetPhysicalDeviceProperties(vkrt->physicalDevice, &deviceProperties);

    printf("INFO: Using device [%s].\n", deviceProperties.deviceName);
    snprintf(vkrt->deviceName, COUNT_OF(vkrt->deviceName), "%s", deviceProperties.deviceName);
    free(devices);
}

void createLogicalDevice(VKRT* vkrt) {
    QueueFamily indices = findQueueFamilies(vkrt);
    vkrt->indices = indices;

    float queuePriority = 1.0f;
    uint32_t uniqueQueueFamilies[2] = {indices.graphics, indices.present};
    uint32_t uniqueQueueFamilyCount = 2;

    VkDeviceQueueCreateInfo* queueCreateInfos = (VkDeviceQueueCreateInfo*)malloc(uniqueQueueFamilyCount * sizeof(VkDeviceQueueCreateInfo));
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

    VkPhysicalDeviceRobustness2FeaturesEXT robustness2Features = {0};
    robustness2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;
    robustness2Features.nullDescriptor = VK_TRUE;

    VkPhysicalDeviceBufferDeviceAddressFeatures deviceBufferDeviceAddressFeatures = {0};
    deviceBufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;
    deviceBufferDeviceAddressFeatures.pNext = &robustness2Features;
    deviceBufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR deviceAccelerationStructureFeatures = {0};
    deviceAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    deviceAccelerationStructureFeatures.pNext = &deviceBufferDeviceAddressFeatures;
    deviceAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
    deviceAccelerationStructureFeatures.accelerationStructureCaptureReplay = VK_FALSE;
    deviceAccelerationStructureFeatures.accelerationStructureIndirectBuild = VK_FALSE;
    deviceAccelerationStructureFeatures.accelerationStructureHostCommands = VK_FALSE;
    deviceAccelerationStructureFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_FALSE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR deviceRayTracingPipelineFeatures = {0};
    deviceRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    deviceRayTracingPipelineFeatures.pNext = &deviceAccelerationStructureFeatures;
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

    if (vkCreateDevice(vkrt->physicalDevice, &createInfo, NULL, &vkrt->device) != VK_SUCCESS) {
        perror("ERROR: Failed to create logical device");
        exit(EXIT_FAILURE);
    }

    vkGetDeviceQueue(vkrt->device, indices.graphics, 0, &vkrt->graphicsQueue);
    vkGetDeviceQueue(vkrt->device, indices.present, 0, &vkrt->presentQueue);

    free(queueCreateInfos);
}

void createQueryPool(VKRT* vkrt) {
    VkQueryPoolCreateInfo qp = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = MAX_FRAMES_IN_FLIGHT * 2
    };

    if (vkCreateQueryPool(vkrt->device, &qp, NULL, &vkrt->timestampPool) != VK_SUCCESS) {
        perror("ERROR: Failed to create timestamp query pool");
        exit(EXIT_FAILURE);
    }

    VkPhysicalDeviceProperties deviceProperties = {0};
    vkGetPhysicalDeviceProperties(vkrt->physicalDevice, &deviceProperties);
    vkrt->timestampPeriod = deviceProperties.limits.timestampPeriod;
}

QueueFamily findQueueFamilies(VKRT* vkrt) {
    QueueFamily indices;
    indices.graphics = -1;
    indices.present = -1;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vkrt->physicalDevice, &queueFamilyCount, NULL);

    VkQueueFamilyProperties* queueFamilies = (VkQueueFamilyProperties*)malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(vkrt->physicalDevice, &queueFamilyCount, queueFamilies);

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
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

int32_t isDeviceSuitable(VKRT* vkrt) {
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

    if (queueFamilyComplete && extensionSupport && swapChainAdequate) {
        for (uint32_t i = 0; i < COUNT_OF(rankedDeviceTypes); i++) {
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
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, availableExtensions);

    for (uint32_t i = 0; i < NUM_EXTENSIONS; i++) {
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

uint32_t findMemoryType(VKRT* vkrt, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(vkrt->physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    perror("ERROR: Failed to find suitable memory type");
    exit(EXIT_FAILURE);
}

void recordFrameTime(VKRT* vkrt, uint64_t startTime) {
    uint64_t currentTime = getMicroseconds();
    vkrt->displayTimeMs = (currentTime - startTime) / 1000.0f;

    uint64_t ts[2];
    vkGetQueryPoolResults(vkrt->device, vkrt->timestampPool, vkrt->currentFrame * 2, 2, sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    vkrt->renderTimeMs = (float)((ts[1] - ts[0]) * vkrt->timestampPeriod / 1e6);

    uint32_t frameNumber = vkrt->sceneData->frameNumber;
    if (frameNumber > 3 && frameNumber % 2 == 0) {
        vkrt->sceneData->samplesPerPixel = (uint32_t)glm_clamp(vkrt->displayTimeMs / vkrt->renderTimeMs * vkrt->sceneData->samplesPerPixel, 1, 1024);
    }

    // rolling frametime average
    float weight = 1.f / (min(1 + frameNumber, COUNT_OF(vkrt->frametimes)));
    vkrt->averageFrametime = vkrt->averageFrametime * (1 - weight) + vkrt->displayTimeMs * weight;
    vkrt->framesPerSecond = (uint32_t)(1000.0f / vkrt->displayTimeMs);

    vkrt->frametimes[vkrt->frametimeStartIndex] = vkrt->displayTimeMs;
    vkrt->frametimeStartIndex = (vkrt->frametimeStartIndex + 1) % COUNT_OF(vkrt->frametimes);
    vkrt->sceneData->frameNumber++;
}

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    uint64_t getMicroseconds() {
        LARGE_INTEGER frequency, counter;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&counter);
        return (uint64_t)(counter.QuadPart * 1000000 / frequency.QuadPart);
    }
#else
    #include <time.h>
    uint64_t getMicroseconds() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    }
#endif
