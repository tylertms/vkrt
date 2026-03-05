#pragma once

#include "vkrt_internal.h"

#define NUM_EXTENSIONS 6
extern const char* deviceExtensions[NUM_EXTENSIONS];

VKRT_Result pickPhysicalDevice(VKRT* vkrt);
VKRT_Result createLogicalDevice(VKRT* vkrt);
VKRT_Result createQueryPool(VKRT* vkrt);
int32_t isDeviceSuitable(VKRT* vkrt);
VkBool32 isQueueFamilyComplete(QueueFamily indices);
QueueFamily findQueueFamilies(VKRT* vkrt);
VkBool32 extensionsSupported(VkPhysicalDevice device);
VKRT_Result findMemoryType(VKRT* vkrt, uint32_t typeFilter, VkMemoryPropertyFlags properties, uint32_t* outMemoryTypeIndex);
