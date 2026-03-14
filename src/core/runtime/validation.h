#pragma once

#include "vkrt_internal.h"

extern const char* validationLayers[];
extern const uint32_t numValidationLayers;
extern const VkBool32 enableValidationLayers;

int checkValidationLayerSupport(void);
const char** getRequiredExtensions(uint32_t* extensionCount, VkBool32 requirePresentation);

void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo);
VKRT_Result setupDebugMessenger(VKRT* vkrt);

VkResult createDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger);
void destroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);

const char* severityString(VkDebugUtilsMessageSeverityFlagBitsEXT severity);
const char* typeString(VkDebugUtilsMessageTypeFlagsEXT type);
