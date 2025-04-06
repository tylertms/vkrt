#include "vulkan.h"
#include "device.h"
#include "instance.h"
#include "surface.h"
#include "validation.h"

void initVulkan(VKRT* vkrt) {
    createInstance(vkrt);
    setupDebugMessenger(vkrt);
    createSurface(vkrt);
    pickPhysicalDevice(vkrt);
    createLogicalDevice(vkrt);
}