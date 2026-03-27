#pragma once

#include "vkrt_internal.h"

#define NUM_REQ_EXTENSIONS 5
#define NUM_OPT_EXTENSIONS 1

extern const char* requiredDeviceExtensions[NUM_REQ_EXTENSIONS];
extern const char* optionalDeviceExtensions[NUM_OPT_EXTENSIONS];
extern const uint32_t requiredDeviceExtensionBits[NUM_REQ_EXTENSIONS];
extern const uint32_t optionalDeviceExtensionBits[NUM_OPT_EXTENSIONS];

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
