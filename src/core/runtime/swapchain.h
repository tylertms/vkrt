#pragma once

#include "vkrt_internal.h"

typedef struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR* formats;
    uint32_t formatCount;
    VkPresentModeKHR* presentModes;
    uint32_t presentModeCount;
} SwapChainSupportDetails;

void vkrtQueryDisplayMetrics(GLFWwindow* window, uint32_t* outWidth, uint32_t* outHeight, float* outRefreshHz);
VkBool32 vkrtUsesRenderPresentProfile(const VKRT* vkrt);
void vkrtRefreshPresentModeIfNeeded(VKRT* vkrt, VkBool32 previousUsesRenderPresentProfile);
VKRT_Result createSwapChain(VKRT* vkrt);
VKRT_Result recreateSwapChain(VKRT* vkrt);
void cleanupSwapChain(VKRT* vkrt);
VKRT_Result createImageViews(VKRT* vkrt);
VKRT_Result createFramebuffers(VKRT* vkrt);
VKRT_Result querySwapChainSupport(VKRT* vkrt, SwapChainSupportDetails* outSupportDetails);
VKRT_Result chooseSwapSurfaceFormat(const SwapChainSupportDetails* supportDetails, VkSurfaceFormatKHR* outSurfaceFormat);
VkPresentModeKHR chooseSwapPresentMode(
    const SwapChainSupportDetails* supportDetails,
    VkBool32 useRenderPresentProfile);
VkExtent2D chooseSwapExtent(VKRT* vkrt, const SwapChainSupportDetails* supportDetails);
