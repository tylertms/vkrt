#pragma once

#include "vkrt_types.h"

#include <vulkan/vulkan.h>

void vkrtClampViewportRect(VkExtent2D extent, uint32_t* x, uint32_t* y, uint32_t* width, uint32_t* height);
void vkrtQueryRenderViewCropExtent(
    VkExtent2D renderExtent,
    VkExtent2D viewportExtent,
    float zoom,
    uint32_t* outWidth,
    uint32_t* outHeight,
    VkBool32* outFillViewport
);
void vkrtClampRenderViewPanOffset(
    VkExtent2D renderExtent,
    VkExtent2D viewportExtent,
    float zoom,
    float* panX,
    float* panY
);
