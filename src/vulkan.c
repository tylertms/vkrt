#include "vulkan.h"
#include "instance.h"
#include "validation.h"

void initVulkan(VKRT* vkrt) {
    createInstance(vkrt);
    setupDebugMessenger(vkrt);
}