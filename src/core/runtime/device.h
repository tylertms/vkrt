#pragma once

#include "vkrt_internal.h"

enum {
    K_REQUIRED_DEVICE_EXTENSION_COUNT = 5,
    K_OPTIONAL_DEVICE_EXTENSION_COUNT = 1,
};

extern const char* requiredDeviceExtensions[K_REQUIRED_DEVICE_EXTENSION_COUNT];
extern const char* optionalDeviceExtensions[K_OPTIONAL_DEVICE_EXTENSION_COUNT];
extern const uint32_t requiredDeviceExtensionBits[K_REQUIRED_DEVICE_EXTENSION_COUNT];
extern const uint32_t optionalDeviceExtensionBits[K_OPTIONAL_DEVICE_EXTENSION_COUNT];

VKRT_Result pickPhysicalDevice(VKRT* vkrt, const VKRT_CreateInfo* createInfo);
VKRT_Result createLogicalDevice(VKRT* vkrt);
VKRT_Result createQueryPool(VKRT* vkrt);
VkBool32 isQueueFamilyComplete(QueueFamily indices);
QueueFamily findQueueFamilies(VKRT* vkrt);
VkBool32 extensionsSupported(VKRT* vkrt, VkPhysicalDevice device, DeviceExtensionSupport* outSupport);
VKRT_Result findMemoryType(
    VKRT* vkrt,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties,
    uint32_t* outMemoryTypeIndex
);
