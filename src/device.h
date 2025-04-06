#pragma once
#include "vkrt.h"

extern const char* deviceExtensions[];
extern const uint32_t numDeviceExtensions;

typedef struct QueueFamily {
    int32_t graphics;
    int32_t present;
} QueueFamily;

void pickPhysicalDevice(VKRT* vkrt);
void createLogicalDevice(VKRT* vkrt);
VkBool32 isDeviceSuitable(VKRT* vkrt);
VkBool32 isQueueFamilyComplete(QueueFamily indices);
QueueFamily findQueueFamilies(VKRT* vkrt);
VkBool32 extensionsSupported(VkPhysicalDevice device);