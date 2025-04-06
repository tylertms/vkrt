#include "vulkan.h"
#include "device.h"
#include "instance.h"
#include "validation.h"

void initVulkan(VKRT* vkrt) {
    createInstance(vkrt);
    setupDebugMessenger(vkrt);
    pickPhysicalDevice(vkrt);
    createLogicalDevice(vkrt);
}