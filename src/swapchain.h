#pragma once
#include "vkrt.h"

typedef struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR* formats;
    uint32_t formatCount;
    VkPresentModeKHR* presentModes;
    uint32_t presentModeCount;
} SwapChainSupportDetails;

void createSwapChain(VKRT* vkrt);
void recreateSwapChain(VKRT* vkrt);
void cleanupSwapChain(VKRT* vkrt);
void createImageViews(VKRT* vkrt);
void createFramebuffers(VKRT* vkrt);
SwapChainSupportDetails querySwapChainSupport(VKRT* vkrt);
VkSurfaceFormatKHR chooseSwapSurfaceFormat(SwapChainSupportDetails* supportDetails);
VkPresentModeKHR chooseSwapPresentMode(SwapChainSupportDetails* supportDetails, uint8_t vsync);
VkExtent2D chooseSwapExtent(VKRT* vkrt, SwapChainSupportDetails* supportDetails);
