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

void vkrtDestroyImageResources(VKRT* vkrt, VkImage* image, VkImageView* view, VkDeviceMemory* memory);
VKRT_Result vkrtCreateDeviceImage(
    VKRT* vkrt,
    VkExtent2D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImage* outImage,
    VkImageView* outView,
    VkDeviceMemory* outMemory
);
VKRT_Result vkrtCreateSampledTextureImageFromData(
    VKRT* vkrt,
    const void* pixelData,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkDeviceSize byteSize,
    VkImage* outImage,
    VkImageView* outView,
    VkDeviceMemory* outMemory
);

VKRT_Result createGPUImages(VKRT* vkrt);
void destroyGPUImages(VKRT* vkrt);
VKRT_Result createGPUImageState(VKRT* vkrt, VkExtent2D extent, GPUImageState* outState);
void destroyGPUImageState(VKRT* vkrt, GPUImageState* state);
void captureGPUImageState(const VKRT* vkrt, GPUImageState* outState);
void applyGPUImageState(VKRT* vkrt, const GPUImageState* state);
