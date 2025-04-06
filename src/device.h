#pragma once
#include "vkrt.h"

typedef struct QueueFamily {
    int32_t graphics;
} QueueFamily;

void pickPhysicalDevice(VKRT* vkrt);
void createLogicalDevice(VKRT* vkrt);
uint8_t isDeviceSuitable(VkPhysicalDevice device);
uint8_t isQueueFamilyComplete(QueueFamily indices);
QueueFamily findQueueFamilies(VkPhysicalDevice device);