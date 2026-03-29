#include "device.h"

#include "config.h"
#include "constants.h"
#include "debug.h"
#include "swapchain.h"
#include "validation.h"
#include "vkrt_engine_types.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

const char* requiredDeviceExtensions[K_REQUIRED_DEVICE_EXTENSION_COUNT] =
    {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
     VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
     VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
     VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
     VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME};

const char* optionalDeviceExtensions[K_OPTIONAL_DEVICE_EXTENSION_COUNT] = {VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME};

const uint32_t requiredDeviceExtensionBits[K_REQUIRED_DEVICE_EXTENSION_COUNT] =
    {DEVICE_EXTENSION_SWAPCHAIN_BIT,
     DEVICE_EXTENSION_ACCELERATION_STRUCTURE_BIT,
     DEVICE_EXTENSION_RAY_TRACING_PIPELINE_BIT,
     DEVICE_EXTENSION_DEFERRED_HOST_OPERATIONS_BIT,
     DEVICE_EXTENSION_BUFFER_DEVICE_ADDRESS_BIT};

const uint32_t optionalDeviceExtensionBits[K_OPTIONAL_DEVICE_EXTENSION_COUNT] = {DEVICE_EXTENSION_RAY_TRACING_INVOCATION_REORDER_BIT};

static const VkPhysicalDeviceType rankedDeviceTypes[4] =
    {VK_PHYSICAL_DEVICE_TYPE_CPU,
     VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
     VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
     VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU};

typedef struct DeviceFeatureChain {
    VkPhysicalDeviceBufferDeviceAddressFeatures deviceBufferDeviceAddressFeatures;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR deviceAccelerationStructureFeatures;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR deviceRayTracingPipelineFeatures;
    VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT deviceReorderFeatures;
    VkPhysicalDeviceDescriptorIndexingFeatures deviceDescriptorIndexingFeatures;
    VkPhysicalDeviceSynchronization2Features deviceSynchronization2Features;
    VkPhysicalDeviceDynamicRenderingFeatures deviceDynamicRenderingFeatures;
} DeviceFeatureChain;

static int32_t scoreDeviceSuitability(VKRT* vkrt, DeviceExtensionSupport* outExtensionSupport);
static VkBool32 deviceMatchesPreference(
    uint32_t deviceIndex,
    const VkPhysicalDeviceProperties* properties,
    const VKRT_CreateInfo* createInfo
);
static void logRequestedDevicePreference(const VKRT_CreateInfo* createInfo);

static DeviceExtensionSupport initDeviceExtensionSupport(void) {
    DeviceExtensionSupport support = {0};

    for (uint32_t i = 0; i < K_REQUIRED_DEVICE_EXTENSION_COUNT; i++) {
        support.requiredMask |= requiredDeviceExtensionBits[i];
    }
    for (uint32_t i = 0; i < K_OPTIONAL_DEVICE_EXTENSION_COUNT; i++) {
        support.optionalMask |= optionalDeviceExtensionBits[i];
    }

    support.missingRequiredMask = support.requiredMask;
    return support;
}

static VkBool32 vkrtRequiresPresentation(const VKRT* vkrt) {
    return vkrt && !vkrt->runtime.headless ? VK_TRUE : VK_FALSE;
}

static uint32_t queryRequiredDeviceExtensionStartIndex(const VKRT* vkrt) {
    return vkrtRequiresPresentation(vkrt) ? 0u : 1u;
}

static void logExtensionMaskStatus(
    const char* groupName,
    const char* const* extensionNames,
    const uint32_t* extensionBits,
    uint32_t extensionCount,
    uint32_t mask,
    const char* presentLabel,
    const char* missingLabel
) {
    for (uint32_t i = 0; i < extensionCount; i++) {
        const char* status = (mask & extensionBits[i]) ? presentLabel : missingLabel;
        LOG_INFO("    [%s] %s: %s", groupName, extensionNames[i], status);
    }
}

static void logMissingRequiredDeviceExtensions(const DeviceExtensionSupport* support) {
    if (!support || !support->missingRequiredMask) return;

    LOG_INFO("    Missing required device extensions:");
    for (uint32_t i = 0; i < K_REQUIRED_DEVICE_EXTENSION_COUNT; i++) {
        if (support->missingRequiredMask & requiredDeviceExtensionBits[i]) {
            LOG_INFO("      %s", requiredDeviceExtensions[i]);
        }
    }
}

static void logDeviceExtensionSupport(const char* deviceName, const DeviceExtensionSupport* support) {
    if (!deviceName || !support) return;

    LOG_INFO("  Extension support for %s:", deviceName);

    logExtensionMaskStatus(
        "required",
        requiredDeviceExtensions,
        requiredDeviceExtensionBits,
        K_REQUIRED_DEVICE_EXTENSION_COUNT,
        support->availableMask,
        "loaded",
        "missing"
    );

    logExtensionMaskStatus(
        "optional",
        optionalDeviceExtensions,
        optionalDeviceExtensionBits,
        K_OPTIONAL_DEVICE_EXTENSION_COUNT,
        support->availableMask,
        "loaded",
        "not loaded"
    );

    logMissingRequiredDeviceExtensions(support);
}

static const char* queryPhysicalDeviceTypeName(VkPhysicalDeviceType deviceType) {
    switch (deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return "Discrete GPU";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return "Integrated GPU";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return "Virtual GPU";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return "CPU";
        default:
            return "Unknown";
    }
}

static void logPhysicalDeviceCandidate(
    uint32_t deviceIndex,
    const VkPhysicalDeviceProperties* properties,
    int32_t score,
    VkBool32 preferenceMatch,
    const DeviceExtensionSupport* extensionSupport
) {
    if (!properties || !extensionSupport) return;

    LOG_INFO(
        "  [%u] %s (%s)%s%s",
        deviceIndex,
        properties->deviceName,
        queryPhysicalDeviceTypeName(properties->deviceType),
        score < 0 ? " - not suitable" : "",
        preferenceMatch ? " [preferred match]" : ""
    );
    logDeviceExtensionSupport(properties->deviceName, extensionSupport);
}

static int buildDeviceQueueCreateInfos(
    QueueFamily indices,
    const float* queuePriority,
    VkDeviceQueueCreateInfo** outCreateInfos,
    uint32_t* outCreateInfoCount
) {
    uint32_t uniqueQueueFamilies[2] = {0u, 0u};
    uint32_t queueCreateInfoCount = 0u;
    VkDeviceQueueCreateInfo* queueCreateInfos = NULL;

    if (!queuePriority || !outCreateInfos || !outCreateInfoCount) return 0;

    uniqueQueueFamilies[0] = (uint32_t)indices.graphics;
    uniqueQueueFamilies[1] = (uint32_t)indices.present;
    queueCreateInfos = (VkDeviceQueueCreateInfo*)malloc(2u * sizeof(VkDeviceQueueCreateInfo));
    if (!queueCreateInfos) return 0;

    for (uint32_t i = 0; i < 2u; i++) {
        VkBool32 duplicate = VK_FALSE;
        for (uint32_t j = 0; j < queueCreateInfoCount; j++) {
            if (uniqueQueueFamilies[i] == queueCreateInfos[j].queueFamilyIndex) {
                duplicate = VK_TRUE;
                break;
            }
        }
        if (duplicate) continue;

        queueCreateInfos[queueCreateInfoCount++] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = uniqueQueueFamilies[i],
            .queueCount = 1u,
            .pQueuePriorities = queuePriority,
        };
    }

    *outCreateInfos = queueCreateInfos;
    *outCreateInfoCount = queueCreateInfoCount;
    return 1;
}

static void initDeviceFeatureChain(DeviceFeatureChain* chain) {
    if (!chain) return;

    *chain = (DeviceFeatureChain){
        .deviceBufferDeviceAddressFeatures =
            {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR,
                .bufferDeviceAddress = VK_TRUE,
            },
        .deviceAccelerationStructureFeatures =
            {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
                .accelerationStructure = VK_TRUE,
            },
        .deviceRayTracingPipelineFeatures =
            {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
                .rayTracingPipeline = VK_TRUE,
            },
        .deviceReorderFeatures =
            {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_EXT,
                .rayTracingInvocationReorder = VK_TRUE,
            },
        .deviceDescriptorIndexingFeatures =
            {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
                .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
            },
        .deviceSynchronization2Features =
            {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
                .synchronization2 = VK_TRUE,
            },
        .deviceDynamicRenderingFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
            .dynamicRendering = VK_TRUE,
        },
    };

    chain->deviceAccelerationStructureFeatures.pNext = &chain->deviceBufferDeviceAddressFeatures;
    chain->deviceRayTracingPipelineFeatures.pNext = &chain->deviceAccelerationStructureFeatures;
    chain->deviceReorderFeatures.pNext = &chain->deviceAccelerationStructureFeatures;
    chain->deviceDescriptorIndexingFeatures.pNext = &chain->deviceRayTracingPipelineFeatures;
    chain->deviceSynchronization2Features.pNext = &chain->deviceDescriptorIndexingFeatures;
    chain->deviceDynamicRenderingFeatures.pNext = &chain->deviceSynchronization2Features;
}

static void querySupportedReorderFeatures(
    VKRT* vkrt,
    DeviceExtensionSupport extensionSupport,
    VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT* outSupportedReorderFeatures
) {
    if (!vkrt || !outSupportedReorderFeatures) return;

    *outSupportedReorderFeatures = (VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT){
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_EXT,
    };
    if (!(extensionSupport.availableMask & DEVICE_EXTENSION_RAY_TRACING_INVOCATION_REORDER_BIT)) return;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR supportedRayTracingPipelineFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .pNext = outSupportedReorderFeatures,
    };
    VkPhysicalDeviceFeatures2 supportedFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &supportedRayTracingPipelineFeatures,
    };
    VkPhysicalDeviceRayTracingInvocationReorderPropertiesEXT reorderProperties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_PROPERTIES_EXT,
    };
    VkPhysicalDeviceProperties2 supportedProperties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &reorderProperties,
    };

    vkGetPhysicalDeviceFeatures2(vkrt->core.physicalDevice, &supportedFeatures);
    vkGetPhysicalDeviceProperties2(vkrt->core.physicalDevice, &supportedProperties);

    vkrt->core.serReorderingHintMode = reorderProperties.rayTracingInvocationReorderReorderingHint;
    vkrt->core.serMaxShaderBindingTableRecordIndex = 0u;
}

static void logSERExtensionStatus(
    const VKRT* vkrt,
    DeviceExtensionSupport extensionSupport,
    VkBool32 reorderFeatureSupported
) {
    if (!vkrt) return;

    if (vkrt->runtime.disableSER &&
        (extensionSupport.availableMask & DEVICE_EXTENSION_RAY_TRACING_INVOCATION_REORDER_BIT)) {
        LOG_INFO(
            "    Optional extension %s was available but disabled by request",
            VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME
        );
    }

    if ((extensionSupport.availableMask & DEVICE_EXTENSION_RAY_TRACING_INVOCATION_REORDER_BIT) &&
        !reorderFeatureSupported) {
        LOG_INFO(
            "    Optional extension %s was loaded but its feature is unsupported, so it was "
            "not enabled",
            VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME
        );
    }

    if (extensionSupport.availableMask & DEVICE_EXTENSION_RAY_TRACING_INVOCATION_REORDER_BIT) {
        const char* reorderHintMode =
            vkrt->core.serReorderingHintMode == VK_RAY_TRACING_INVOCATION_REORDER_MODE_REORDER_EXT ? "reorder" : "none";
        LOG_INFO(
            "    SER properties: hint mode=%s, max shader table record index=%u",
            reorderHintMode,
            vkrt->core.serMaxShaderBindingTableRecordIndex
        );
    }
}

static void logEnabledDeviceExtensions(
    const VKRT* vkrt,
    DeviceExtensionSupport extensionSupport,
    VkBool32 reorderFeatureSupported
) {
    if (!vkrt) return;

    LOG_INFO("Enabling device extensions for %s:", vkrt->core.deviceName);
    logExtensionMaskStatus(
        "required",
        requiredDeviceExtensions,
        requiredDeviceExtensionBits,
        K_REQUIRED_DEVICE_EXTENSION_COUNT,
        extensionSupport.enabledMask,
        "enabled",
        "disabled"
    );
    logExtensionMaskStatus(
        "optional",
        optionalDeviceExtensions,
        optionalDeviceExtensionBits,
        K_OPTIONAL_DEVICE_EXTENSION_COUNT,
        extensionSupport.enabledMask,
        "enabled",
        "disabled"
    );

    logSERExtensionStatus(vkrt, extensionSupport, reorderFeatureSupported);
}

static VkBool32 isDevicePreferenceRequested(const VKRT_CreateInfo* createInfo) {
    return createInfo && (createInfo->preferredDeviceIndex >= 0 ||
                          (createInfo->preferredDeviceName && createInfo->preferredDeviceName[0]));
}

static VKRT_Result enumeratePhysicalDevices(
    VkInstance instance,
    VkPhysicalDevice** outDevices,
    uint32_t* outDeviceCount
) {
    uint32_t deviceCount = 0u;
    VkPhysicalDevice* devices = NULL;

    if (!outDevices || !outDeviceCount) return VKRT_ERROR_INVALID_ARGUMENT;
    *outDevices = NULL;
    *outDeviceCount = 0u;

    if (vkEnumeratePhysicalDevices(instance, &deviceCount, NULL) != VK_SUCCESS) {
        LOG_ERROR("Failed to enumerate physical devices");
        return VKRT_ERROR_INITIALIZATION_FAILED;
    }
    if (deviceCount == 0u) {
        LOG_ERROR("Failed to find a GPU with Vulkan support");
        return VKRT_ERROR_INITIALIZATION_FAILED;
    }

    devices = (VkPhysicalDevice*)malloc(deviceCount * sizeof(VkPhysicalDevice));
    if (!devices) return VKRT_ERROR_OUT_OF_MEMORY;
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, devices) != VK_SUCCESS) {
        LOG_ERROR("Failed to enumerate physical devices");
        free((void*)devices);
        return VKRT_ERROR_INITIALIZATION_FAILED;
    }

    *outDevices = devices;
    *outDeviceCount = deviceCount;
    return VKRT_SUCCESS;
}

static void evaluatePhysicalDeviceList(
    VKRT* vkrt,
    const VKRT_CreateInfo* createInfo,
    const VkPhysicalDevice* devices,
    uint32_t deviceCount,
    int32_t* outBestDevice,
    VkBool32* outPreferenceFound
) {
    int32_t highestScore = -1;
    VkBool32 preferenceRequested = isDevicePreferenceRequested(createInfo);

    if (!vkrt || !devices || !outBestDevice || !outPreferenceFound) return;

    *outBestDevice = -1;
    *outPreferenceFound = VK_FALSE;
    LOG_INFO("Found %u Vulkan device(s):", deviceCount);
    logRequestedDevicePreference(createInfo);

    for (uint32_t i = 0; i < deviceCount; i++) {
        VkPhysicalDeviceProperties props = {0};
        DeviceExtensionSupport extensionSupport = {0};
        int32_t score = -1;
        VkBool32 preferenceMatch = VK_FALSE;

        vkGetPhysicalDeviceProperties(devices[i], &props);
        vkrt->core.physicalDevice = devices[i];
        score = scoreDeviceSuitability(vkrt, &extensionSupport);
        preferenceMatch = deviceMatchesPreference(i, &props, createInfo);
        if (preferenceMatch) *outPreferenceFound = VK_TRUE;

        logPhysicalDeviceCandidate(i, &props, score, preferenceMatch, &extensionSupport);
        if (score > highestScore && (!preferenceRequested || preferenceMatch)) {
            highestScore = score;
            *outBestDevice = (int32_t)i;
        }
    }
}

static void storeSelectedPhysicalDeviceInfo(VKRT* vkrt, const VkPhysicalDevice* devices, int32_t bestDevice) {
    VkPhysicalDeviceProperties deviceProperties = {0};

    if (!vkrt || !devices || bestDevice < 0) return;

    vkrt->core.physicalDevice = devices[bestDevice];
    extensionsSupported(vkrt, vkrt->core.physicalDevice, &vkrt->core.deviceExtensionSupport);
    vkGetPhysicalDeviceProperties(vkrt->core.physicalDevice, &deviceProperties);

    LOG_INFO("Selected device [%u]: %s", (uint32_t)bestDevice, deviceProperties.deviceName);
    (void)snprintf(vkrt->core.deviceName, sizeof(vkrt->core.deviceName), "%s", deviceProperties.deviceName);
    vkrt->core.vendorID = deviceProperties.vendorID;
    vkrt->core.driverVersion = deviceProperties.driverVersion;
}

static int32_t scoreDeviceSuitability(VKRT* vkrt, DeviceExtensionSupport* outExtensionSupport) {
    DeviceExtensionSupport extensionSupport = {0};
    VkPhysicalDeviceProperties deviceProperties = {0};
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {0};
    bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {0};
    accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    accelerationStructureFeatures.pNext = &bufferDeviceAddressFeatures;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures = {0};
    rayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rayTracingPipelineFeatures.pNext = &accelerationStructureFeatures;

    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures = {0};
    descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    descriptorIndexingFeatures.pNext = &rayTracingPipelineFeatures;

    VkPhysicalDeviceSynchronization2Features synchronization2Features = {0};
    synchronization2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    synchronization2Features.pNext = &descriptorIndexingFeatures;

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures = {0};
    dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    dynamicRenderingFeatures.pNext = &synchronization2Features;

    VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {0};
    physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    physicalDeviceFeatures.pNext = &dynamicRenderingFeatures;

    vkGetPhysicalDeviceProperties(vkrt->core.physicalDevice, &deviceProperties);
    vkGetPhysicalDeviceFeatures2(vkrt->core.physicalDevice, &physicalDeviceFeatures);

    QueueFamily indices = findQueueFamilies(vkrt);
    VkBool32 queueFamilyComplete = isQueueFamilyComplete(indices);

    VkBool32 requiredExtensionsSupported = extensionsSupported(vkrt, vkrt->core.physicalDevice, &extensionSupport);
    if (outExtensionSupport) {
        *outExtensionSupport = extensionSupport;
    }

    VkBool32 swapChainAdequate = vkrtRequiresPresentation(vkrt) ? VK_FALSE : VK_TRUE;
    if (requiredExtensionsSupported && vkrtRequiresPresentation(vkrt)) {
        SwapChainSupportDetails supportDetails = {
            .capabilities = {.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR},
        };
        if (querySwapChainSupport(vkrt, &supportDetails) == VKRT_SUCCESS) {
            swapChainAdequate = supportDetails.formatCount && supportDetails.presentModeCount;
        }

        free((void*)supportDetails.formats);
        free((void*)supportDetails.presentModes);
    }

    VkBool32 requiredFeatures =
        bufferDeviceAddressFeatures.bufferDeviceAddress && accelerationStructureFeatures.accelerationStructure &&
        rayTracingPipelineFeatures.rayTracingPipeline && dynamicRenderingFeatures.dynamicRendering &&
        synchronization2Features.synchronization2 &&
        descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing &&
        deviceProperties.limits.maxPerStageDescriptorSampledImages >= VKRT_MAX_BINDLESS_TEXTURES &&
        deviceProperties.limits.maxDescriptorSetSampledImages >= VKRT_MAX_BINDLESS_TEXTURES;

    if (queueFamilyComplete && requiredExtensionsSupported && swapChainAdequate && requiredFeatures) {
        for (uint32_t i = 0; i < VKRT_ARRAY_COUNT(rankedDeviceTypes); i++) {
            if (deviceProperties.deviceType == rankedDeviceTypes[i]) {
                return (int32_t)i;
            }
        }
    }

    return -1;
}

static int charsEqualCaseInsensitive(char lhs, char rhs) {
    return tolower((unsigned char)lhs) == tolower((unsigned char)rhs);
}

static int startsWithCaseInsensitive(const char* text, const char* prefix) {
    if (!text || !prefix) return 0;

    while (*prefix) {
        if (!*text || !charsEqualCaseInsensitive(*text, *prefix)) {
            return 0;
        }
        text++;
        prefix++;
    }
    return 1;
}

static int stringContainsCaseInsensitive(const char* text, const char* pattern) {
    if (!text || !pattern) return 0;
    if (!pattern[0]) return 1;

    for (const char* cursor = text; cursor[0]; cursor++) {
        if (startsWithCaseInsensitive(cursor, pattern)) {
            return 1;
        }
    }
    return 0;
}

static VkBool32 deviceMatchesPreference(
    uint32_t deviceIndex,
    const VkPhysicalDeviceProperties* properties,
    const VKRT_CreateInfo* createInfo
) {
    if (!properties) return VK_FALSE;
    if (!createInfo) return VK_TRUE;

    if (createInfo->preferredDeviceIndex >= 0 && createInfo->preferredDeviceIndex != (int32_t)deviceIndex) {
        return VK_FALSE;
    }

    if (createInfo->preferredDeviceName && createInfo->preferredDeviceName[0] &&
        !stringContainsCaseInsensitive(properties->deviceName, createInfo->preferredDeviceName)) {
        return VK_FALSE;
    }

    return VK_TRUE;
}

static void logRequestedDevicePreference(const VKRT_CreateInfo* createInfo) {
    if (!createInfo) return;
    if (createInfo->preferredDeviceIndex < 0 &&
        (!createInfo->preferredDeviceName || !createInfo->preferredDeviceName[0])) {
        return;
    }

    if (createInfo->preferredDeviceIndex >= 0 && createInfo->preferredDeviceName &&
        createInfo->preferredDeviceName[0]) {
        LOG_INFO(
            "Requested device: index %d, name contains \"%s\"",
            createInfo->preferredDeviceIndex,
            createInfo->preferredDeviceName
        );
        return;
    }
    if (createInfo->preferredDeviceIndex >= 0) {
        LOG_INFO("Requested device: index %d", createInfo->preferredDeviceIndex);
        return;
    }

    LOG_INFO("Requested device: name contains \"%s\"", createInfo->preferredDeviceName);
}

static void formatDevicePreferenceText(const VKRT_CreateInfo* createInfo, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';

    if (!createInfo) {
        (void)snprintf(out, outSize, "default selection");
        return;
    }

    if (createInfo->preferredDeviceIndex >= 0 && createInfo->preferredDeviceName &&
        createInfo->preferredDeviceName[0]) {
        (void)snprintf(
            out,
            outSize,
            "index %d, name contains \"%s\"",
            createInfo->preferredDeviceIndex,
            createInfo->preferredDeviceName
        );
        return;
    }

    if (createInfo->preferredDeviceIndex >= 0) {
        (void)snprintf(out, outSize, "index %d", createInfo->preferredDeviceIndex);
        return;
    }

    if (createInfo->preferredDeviceName && createInfo->preferredDeviceName[0]) {
        (void)snprintf(out, outSize, "name contains \"%s\"", createInfo->preferredDeviceName);
        return;
    }

    (void)snprintf(out, outSize, "default selection");
}

VKRT_Result pickPhysicalDevice(VKRT* vkrt, const VKRT_CreateInfo* createInfo) {
    VkPhysicalDevice* devices = NULL;
    uint32_t deviceCount = 0u;
    int32_t bestDevice = -1;
    VkBool32 preferenceFound = VK_FALSE;
    VkBool32 preferenceRequested = isDevicePreferenceRequested(createInfo);
    VKRT_Result result = VKRT_SUCCESS;

    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    vkrt->core.physicalDevice = VK_NULL_HANDLE;

    result = enumeratePhysicalDevices(vkrt->core.instance, &devices, &deviceCount);
    if (result != VKRT_SUCCESS) return result;

    evaluatePhysicalDeviceList(vkrt, createInfo, devices, deviceCount, &bestDevice, &preferenceFound);

    if (preferenceRequested && !preferenceFound) {
        char preferenceText[VKRT_NAME_LEN];
        formatDevicePreferenceText(createInfo, preferenceText, sizeof(preferenceText));
        LOG_ERROR("Requested Vulkan device was not found. Preference: %s", preferenceText);
        free((void*)devices);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (bestDevice < 0) {
        if (preferenceRequested) {
            char preferenceText[VKRT_NAME_LEN];
            formatDevicePreferenceText(createInfo, preferenceText, sizeof(preferenceText));
            LOG_ERROR("Requested Vulkan device is not suitable. Preference: %s", preferenceText);
        } else {
            LOG_ERROR("Failed to find a suitable GPU");
        }
        free((void*)devices);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    storeSelectedPhysicalDeviceInfo(vkrt, devices, bestDevice);
    free((void*)devices);
    return VKRT_SUCCESS;
}

VKRT_Result createLogicalDevice(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    DeviceExtensionSupport extensionSupport = {0};
    vkrt->core.serReorderingHintMode = VK_RAY_TRACING_INVOCATION_REORDER_MODE_NONE_EXT;
    vkrt->core.serMaxShaderBindingTableRecordIndex = 0u;
    QueueFamily indices = findQueueFamilies(vkrt);
    if (!isQueueFamilyComplete(indices)) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
    vkrt->core.indices = indices;

    if (!extensionsSupported(vkrt, vkrt->core.physicalDevice, &extensionSupport)) {
        LOG_ERROR("Selected device is missing required device extensions");
        logDeviceExtensionSupport(vkrt->core.deviceName, &extensionSupport);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo* queueCreateInfos = NULL;
    uint32_t queueCreateInfoCount = 0u;
    if (!buildDeviceQueueCreateInfos(indices, &queuePriority, &queueCreateInfos, &queueCreateInfoCount)) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    DeviceFeatureChain featureChain;
    initDeviceFeatureChain(&featureChain);

    VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT supportedReorderFeatures;
    querySupportedReorderFeatures(vkrt, extensionSupport, &supportedReorderFeatures);

    const char* enabledExtensions[K_REQUIRED_DEVICE_EXTENSION_COUNT + K_OPTIONAL_DEVICE_EXTENSION_COUNT + 1] = {0};
    uint32_t enabledExtensionCount = 0;
    uint32_t requiredExtensionStart = queryRequiredDeviceExtensionStartIndex(vkrt);
    for (uint32_t i = requiredExtensionStart; i < K_REQUIRED_DEVICE_EXTENSION_COUNT; i++) {
        enabledExtensions[enabledExtensionCount++] = requiredDeviceExtensions[i];
        extensionSupport.enabledMask |= requiredDeviceExtensionBits[i];
    }

    if (!vkrt->runtime.disableSER &&
        (extensionSupport.availableMask & DEVICE_EXTENSION_RAY_TRACING_INVOCATION_REORDER_BIT) &&
        supportedReorderFeatures.rayTracingInvocationReorder) {
        enabledExtensions[enabledExtensionCount++] = optionalDeviceExtensions[0];
        extensionSupport.enabledMask |= DEVICE_EXTENSION_RAY_TRACING_INVOCATION_REORDER_BIT;
        featureChain.deviceRayTracingPipelineFeatures.pNext = &featureChain.deviceReorderFeatures;
    }

    vkrt->core.deviceExtensionSupport = extensionSupport;

    VkPhysicalDeviceFeatures deviceFeatures = {0};

    VkDeviceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = queueCreateInfos;
    createInfo.queueCreateInfoCount = queueCreateInfoCount;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.pNext = &featureChain.deviceDynamicRenderingFeatures;
    createInfo.enabledExtensionCount = enabledExtensionCount;
    createInfo.ppEnabledExtensionNames = enabledExtensions;

    logEnabledDeviceExtensions(vkrt, extensionSupport, supportedReorderFeatures.rayTracingInvocationReorder);

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = numValidationLayers;
        createInfo.ppEnabledLayerNames = validationLayers;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(vkrt->core.physicalDevice, &createInfo, NULL, &vkrt->core.device) != VK_SUCCESS) {
        LOG_ERROR("Failed to create logical device");
        free((void*)queueCreateInfos);
        return VKRT_ERROR_INITIALIZATION_FAILED;
    }

    vkGetDeviceQueue(vkrt->core.device, indices.graphics, 0, &vkrt->core.graphicsQueue);
    vkGetDeviceQueue(vkrt->core.device, indices.present, 0, &vkrt->core.presentQueue);

    free((void*)queueCreateInfos);
    return VKRT_SUCCESS;
}

VKRT_Result createQueryPool(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    VkQueryPoolCreateInfo queryPoolCreateInfo =
        {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
         .queryType = VK_QUERY_TYPE_TIMESTAMP,
         .queryCount = VKRT_MAX_FRAMES_IN_FLIGHT * 2};

    if (vkCreateQueryPool(vkrt->core.device, &queryPoolCreateInfo, NULL, &vkrt->runtime.timestampPool) != VK_SUCCESS) {
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

    VkQueueFamilyProperties* queueFamilies =
        (VkQueueFamilyProperties*)malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    if (!queueFamilies) {
        return indices;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(vkrt->core.physicalDevice, &queueFamilyCount, queueFamilies);

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = (int32_t)i;
            if (vkrt && (vkrt->runtime.headless || vkrt->runtime.surface == VK_NULL_HANDLE)) {
                indices.present = (int32_t)i;
            }
        }

        if (vkrt && !(vkrt->runtime.headless || vkrt->runtime.surface == VK_NULL_HANDLE)) {
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(vkrt->core.physicalDevice, i, vkrt->runtime.surface, &presentSupport);

            if (presentSupport) {
                indices.present = (int32_t)i;
            }
        }

        if (isQueueFamilyComplete(indices)) {
            break;
        }
    }

    free((void*)queueFamilies);

    return indices;
}

VkBool32 isQueueFamilyComplete(QueueFamily indices) {
    return indices.graphics >= 0 && indices.present >= 0;
}

VkBool32 extensionsSupported(VKRT* vkrt, VkPhysicalDevice device, DeviceExtensionSupport* outSupport) {
    uint32_t extensionCount = 0;
    DeviceExtensionSupport support = initDeviceExtensionSupport();
    uint32_t requiredExtensionStart = queryRequiredDeviceExtensionStartIndex(vkrt);
    if (vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL) != VK_SUCCESS) {
        if (outSupport) *outSupport = support;
        return VK_FALSE;
    }

    VkExtensionProperties* availableExtensions =
        (VkExtensionProperties*)malloc(extensionCount * sizeof(VkExtensionProperties));
    if (!availableExtensions) {
        if (outSupport) *outSupport = support;
        return VK_FALSE;
    }
    if (vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, availableExtensions) != VK_SUCCESS) {
        free((void*)availableExtensions);
        if (outSupport) *outSupport = support;
        return VK_FALSE;
    }

    support.requiredMask = 0;
    for (uint32_t i = requiredExtensionStart; i < K_REQUIRED_DEVICE_EXTENSION_COUNT; i++) {
        support.requiredMask |= requiredDeviceExtensionBits[i];
    }
    support.missingRequiredMask = support.requiredMask;

    for (uint32_t i = requiredExtensionStart; i < K_REQUIRED_DEVICE_EXTENSION_COUNT; i++) {
        for (uint32_t j = 0; j < extensionCount; j++) {
            if (!strcmp(requiredDeviceExtensions[i], availableExtensions[j].extensionName)) {
                support.availableMask |= requiredDeviceExtensionBits[i];
                break;
            }
        }
    }

    for (uint32_t i = 0; i < K_OPTIONAL_DEVICE_EXTENSION_COUNT; i++) {
        for (uint32_t j = 0; j < extensionCount; j++) {
            if (!strcmp(optionalDeviceExtensions[i], availableExtensions[j].extensionName)) {
                support.availableMask |= optionalDeviceExtensionBits[i];
                break;
            }
        }
    }

    support.missingRequiredMask = support.requiredMask & ~support.availableMask;

    free((void*)availableExtensions);
    if (outSupport) *outSupport = support;
    return support.missingRequiredMask == 0;
}

VKRT_Result findMemoryType(
    VKRT* vkrt,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties,
    uint32_t* outMemoryTypeIndex
) {
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
