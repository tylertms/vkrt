#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "vkrt_types.h"

typedef struct VKRT_AppHooks {
    void (*init)(struct VKRT* vkrt, void* userData);
    void (*deinit)(struct VKRT* vkrt, void* userData);
    void (*drawOverlay)(struct VKRT* vkrt, VkCommandBuffer commandBuffer, void* userData);
    void* userData;
} VKRT_AppHooks;

typedef struct VKRT_OverlayInfo {
    GLFWwindow* window;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t graphicsQueueFamily;
    VkQueue graphicsQueue;
    VkDescriptorPool descriptorPool;
    VkRenderPass renderPass;
    uint32_t swapchainImageCount;
    uint32_t swapchainMinImageCount;
} VKRT_OverlayInfo;

#ifdef __cplusplus
extern "C" {
#endif

void VKRT_registerAppHooks(VKRT* vkrt, VKRT_AppHooks hooks);
VKRT_Result VKRT_getOverlayInfo(const VKRT* vkrt, VKRT_OverlayInfo* outOverlayInfo);
void VKRT_framebufferResizedCallback(GLFWwindow* window, int width, int height);

#ifdef __cplusplus
}
#endif
