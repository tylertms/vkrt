#pragma once

#include "vkrt_internal.h"

typedef struct GPUImageState {
    VkImage outputImage;
    VkImageView outputImageView;
    VkDeviceMemory outputImageMemory;
    VkImage accumulationImages[2];
    VkImageView accumulationImageViews[2];
    VkDeviceMemory accumulationImageMemories[2];
    VkImage selectionMaskImage;
    VkImageView selectionMaskImageView;
    VkDeviceMemory selectionMaskImageMemory;
} GPUImageState;

VKRT_Result createGPUImages(VKRT* vkrt);
void destroyGPUImages(VKRT* vkrt);
VKRT_Result createGPUImageState(VKRT* vkrt, VkExtent2D extent, GPUImageState* outState);
void destroyGPUImageState(VKRT* vkrt, GPUImageState* state);
void captureGPUImageState(const VKRT* vkrt, GPUImageState* outState);
void applyGPUImageState(VKRT* vkrt, const GPUImageState* state);
