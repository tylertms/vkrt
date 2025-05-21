#pragma once
#include "vkrt.h"

#define NUM_EXTENSIONS 5
extern const char* deviceExtensions[NUM_EXTENSIONS];

typedef struct QueueFamily {
    int32_t graphics;
    int32_t present;
} QueueFamily;

void pickPhysicalDevice(VKRT* vkrt);
void createLogicalDevice(VKRT* vkrt);
int32_t isDeviceSuitable(VKRT* vkrt);
VkBool32 isQueueFamilyComplete(QueueFamily indices);
QueueFamily findQueueFamilies(VKRT* vkrt);
VkBool32 extensionsSupported(VkPhysicalDevice device);
uint32_t findMemoryType(VKRT* vkrt, uint32_t typeFilter, VkMemoryPropertyFlags properties);
void initializeFrameTimers(VKRT* vkrt);
void recordFrameTime(VKRT* vkrt);