#pragma once
#include "vkrt.h"

extern const char* validationLayers[];
extern const uint32_t numValidationLayers;
extern const VkBool32 enableValidationLayers;

int checkValidationLayerSupport();
const char** getRequiredExtensions(uint32_t* extensionCount);

void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT* createInfo);
void setupDebugMessenger(VKRT* vkrt);

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger);
void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);

const char* severityString(VkDebugUtilsMessageSeverityFlagBitsEXT severity);
const char* typeString(VkDebugUtilsMessageTypeFlagsEXT type);