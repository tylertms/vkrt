#pragma once

#include "vkrt_internal.h"

typedef struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR* formats;
    uint32_t formatCount;
    VkPresentModeKHR* presentModes;
    uint32_t presentModeCount;
} SwapChainSupportDetails;

VKRT_Result createSwapChain(VKRT* vkrt);
VKRT_Result recreateSwapChain(VKRT* vkrt);
void cleanupSwapChain(VKRT* vkrt);
VKRT_Result createImageViews(VKRT* vkrt);
VKRT_Result createFramebuffers(VKRT* vkrt);
VKRT_Result querySwapChainSupport(VKRT* vkrt, SwapChainSupportDetails* outSupportDetails);
VKRT_Result chooseSwapSurfaceFormat(const SwapChainSupportDetails* supportDetails, VkSurfaceFormatKHR* outSurfaceFormat);
VkPresentModeKHR chooseSwapPresentMode(SwapChainSupportDetails* supportDetails, uint8_t vsync);
VkExtent2D chooseSwapExtent(VKRT* vkrt, SwapChainSupportDetails* supportDetails);
