#include "images.h"

#include "buffer.h"
#include "command/pool.h"
#include "command/record.h"
#include "device.h"
#include "scene.h"
#include "debug.h"

#include <string.h>

static VKRT_Result createImageWithMemory(
    VKRT* vkrt,
    VkExtent2D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImage* outImage,
    VkImageView* outView,
    VkDeviceMemory* outMemory
) {
    if (!vkrt || !outImage || !outView || !outMemory) return VKRT_ERROR_INVALID_ARGUMENT;
    if (extent.width == 0 || extent.height == 0) return VKRT_ERROR_INVALID_ARGUMENT;

    *outImage = VK_NULL_HANDLE;
    *outView = VK_NULL_HANDLE;
    *outMemory = VK_NULL_HANDLE;

    VkImageCreateInfo imageCreateInfo = {0};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.extent = (VkExtent3D){extent.width, extent.height, 1u};
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.format = format;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.usage = usage;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(vkrt->core.device, &imageCreateInfo, NULL, outImage) != VK_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkMemoryRequirements memoryRequirements = {0};
    vkGetImageMemoryRequirements(vkrt->core.device, *outImage, &memoryRequirements);

    VkMemoryAllocateInfo memoryAllocateInfo = {0};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    if (findMemoryType(
        vkrt,
        memoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &memoryAllocateInfo.memoryTypeIndex
    ) != VKRT_SUCCESS) {
        vkrtDestroyImageResources(vkrt, outImage, outView, outMemory);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkAllocateMemory(vkrt->core.device, &memoryAllocateInfo, NULL, outMemory) != VK_SUCCESS) {
        vkrtDestroyImageResources(vkrt, outImage, outView, outMemory);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkBindImageMemory(vkrt->core.device, *outImage, *outMemory, 0) != VK_SUCCESS) {
        vkrtDestroyImageResources(vkrt, outImage, outView, outMemory);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkImageViewCreateInfo imageViewCreateInfo = {0};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.image = *outImage;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.format = format;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vkrt->core.device, &imageViewCreateInfo, NULL, outView) != VK_SUCCESS) {
        vkrtDestroyImageResources(vkrt, outImage, outView, outMemory);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    return VKRT_SUCCESS;
}

void vkrtDestroyImageResources(VKRT* vkrt, VkImage* image, VkImageView* view, VkDeviceMemory* memory) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    if (view && *view != VK_NULL_HANDLE) {
        vkDestroyImageView(vkrt->core.device, *view, NULL);
        *view = VK_NULL_HANDLE;
    }
    if (image && *image != VK_NULL_HANDLE) {
        vkDestroyImage(vkrt->core.device, *image, NULL);
        *image = VK_NULL_HANDLE;
    }
    if (memory && *memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, *memory, NULL);
        *memory = VK_NULL_HANDLE;
    }
}

VKRT_Result vkrtCreateDeviceImage(
    VKRT* vkrt,
    VkExtent2D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImage* outImage,
    VkImageView* outView,
    VkDeviceMemory* outMemory
) {
    return createImageWithMemory(vkrt, extent, format, usage, outImage, outView, outMemory);
}

VKRT_Result vkrtCreateSampledTextureImageFromData(
    VKRT* vkrt,
    const void* pixels,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkDeviceSize byteSize,
    VkImage* outImage,
    VkImageView* outView,
    VkDeviceMemory* outMemory
) {
    if (!vkrt || !pixels || width == 0u || height == 0u || !outImage || !outView || !outMemory || byteSize == 0u) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    VKRT_Result result = createImageWithMemory(
        vkrt,
        (VkExtent2D){width, height},
        format,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        outImage,
        outView,
        outMemory
    );
    if (result != VKRT_SUCCESS) {
        return result;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    result = createBuffer(
        vkrt,
        byteSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer,
        &stagingMemory
    );
    if (result != VKRT_SUCCESS) {
        vkrtDestroyImageResources(vkrt, outImage, outView, outMemory);
        return result;
    }

    void* mapped = NULL;
    if (vkMapMemory(vkrt->core.device, stagingMemory, 0, byteSize, 0, &mapped) != VK_SUCCESS || !mapped) {
        vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
        vkrtDestroyImageResources(vkrt, outImage, outView, outMemory);
        return VKRT_ERROR_OPERATION_FAILED;
    }
    memcpy(mapped, pixels, (size_t)byteSize);
    vkUnmapMemory(vkrt->core.device, stagingMemory);

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    result = beginSingleTimeCommands(vkrt, &commandBuffer);
    if (result != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
        vkrtDestroyImageResources(vkrt, outImage, outView, outMemory);
        return result;
    }

    transitionImageLayout(commandBuffer, *outImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy copyRegion = {0};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = (VkExtent3D){width, height, 1u};

    vkCmdCopyBufferToImage(
        commandBuffer,
        stagingBuffer,
        *outImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion
    );
    transitionImageLayout(commandBuffer, *outImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    result = endSingleTimeCommands(vkrt, commandBuffer);
    vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
    vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
    if (result != VKRT_SUCCESS) {
        vkrtDestroyImageResources(vkrt, outImage, outView, outMemory);
    }
    return result;
}

typedef struct GPUImageSlot {
    VkImage* image;
    VkImageView* view;
    VkDeviceMemory* memory;
    VkFormat format;
    VkImageUsageFlags usage;
} GPUImageSlot;

static void clearGPUImageBindings(VKRT* vkrt) {
    if (!vkrt) return;

    vkrt->core.outputImage = VK_NULL_HANDLE;
    vkrt->core.outputImageView = VK_NULL_HANDLE;
    vkrt->core.outputImageMemory = VK_NULL_HANDLE;

    for (uint32_t i = 0; i < 2; i++) {
        vkrt->core.accumulationImages[i] = VK_NULL_HANDLE;
        vkrt->core.accumulationImageViews[i] = VK_NULL_HANDLE;
        vkrt->core.accumulationImageMemories[i] = VK_NULL_HANDLE;
        vkrt->core.albedoImages[i] = VK_NULL_HANDLE;
        vkrt->core.albedoImageViews[i] = VK_NULL_HANDLE;
        vkrt->core.albedoImageMemories[i] = VK_NULL_HANDLE;
        vkrt->core.normalImages[i] = VK_NULL_HANDLE;
        vkrt->core.normalImageViews[i] = VK_NULL_HANDLE;
        vkrt->core.normalImageMemories[i] = VK_NULL_HANDLE;
    }

    vkrt->core.selectionMaskImage = VK_NULL_HANDLE;
    vkrt->core.selectionMaskImageView = VK_NULL_HANDLE;
    vkrt->core.selectionMaskImageMemory = VK_NULL_HANDLE;
}

static uint32_t queryGPUImageSlots(GPUImageState* state, GPUImageSlot slots[8]) {
    if (!state || !slots) return 0;

    slots[0] = (GPUImageSlot){
        .image = &state->accumulationImages[0],
        .view = &state->accumulationImageViews[0],
        .memory = &state->accumulationImageMemories[0],
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    slots[1] = (GPUImageSlot){
        .image = &state->accumulationImages[1],
        .view = &state->accumulationImageViews[1],
        .memory = &state->accumulationImageMemories[1],
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    slots[2] = (GPUImageSlot){
        .image = &state->albedoImages[0],
        .view = &state->albedoImageViews[0],
        .memory = &state->albedoImageMemories[0],
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    slots[3] = (GPUImageSlot){
        .image = &state->albedoImages[1],
        .view = &state->albedoImageViews[1],
        .memory = &state->albedoImageMemories[1],
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    slots[4] = (GPUImageSlot){
        .image = &state->normalImages[0],
        .view = &state->normalImageViews[0],
        .memory = &state->normalImageMemories[0],
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    slots[5] = (GPUImageSlot){
        .image = &state->normalImages[1],
        .view = &state->normalImageViews[1],
        .memory = &state->normalImageMemories[1],
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    slots[6] = (GPUImageSlot){
        .image = &state->outputImage,
        .view = &state->outputImageView,
        .memory = &state->outputImageMemory,
        .format = VK_FORMAT_R16G16B16A16_UNORM,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    slots[7] = (GPUImageSlot){
        .image = &state->selectionMaskImage,
        .view = &state->selectionMaskImageView,
        .memory = &state->selectionMaskImageMemory,
        .format = VK_FORMAT_R32_UINT,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    return 8;
}

void captureGPUImageState(const VKRT* vkrt, GPUImageState* outState) {
    if (!outState) return;

    *outState = (GPUImageState){0};
    if (!vkrt) return;

    outState->outputImage = vkrt->core.outputImage;
    outState->outputImageView = vkrt->core.outputImageView;
    outState->outputImageMemory = vkrt->core.outputImageMemory;
    for (uint32_t i = 0; i < 2; i++) {
        outState->accumulationImages[i] = vkrt->core.accumulationImages[i];
        outState->accumulationImageViews[i] = vkrt->core.accumulationImageViews[i];
        outState->accumulationImageMemories[i] = vkrt->core.accumulationImageMemories[i];
        outState->albedoImages[i] = vkrt->core.albedoImages[i];
        outState->albedoImageViews[i] = vkrt->core.albedoImageViews[i];
        outState->albedoImageMemories[i] = vkrt->core.albedoImageMemories[i];
        outState->normalImages[i] = vkrt->core.normalImages[i];
        outState->normalImageViews[i] = vkrt->core.normalImageViews[i];
        outState->normalImageMemories[i] = vkrt->core.normalImageMemories[i];
    }
    outState->selectionMaskImage = vkrt->core.selectionMaskImage;
    outState->selectionMaskImageView = vkrt->core.selectionMaskImageView;
    outState->selectionMaskImageMemory = vkrt->core.selectionMaskImageMemory;
}

void applyGPUImageState(VKRT* vkrt, const GPUImageState* state) {
    if (!vkrt) return;

    clearGPUImageBindings(vkrt);
    if (!state) return;

    vkrt->core.outputImage = state->outputImage;
    vkrt->core.outputImageView = state->outputImageView;
    vkrt->core.outputImageMemory = state->outputImageMemory;
    for (uint32_t i = 0; i < 2; i++) {
        vkrt->core.accumulationImages[i] = state->accumulationImages[i];
        vkrt->core.accumulationImageViews[i] = state->accumulationImageViews[i];
        vkrt->core.accumulationImageMemories[i] = state->accumulationImageMemories[i];
        vkrt->core.albedoImages[i] = state->albedoImages[i];
        vkrt->core.albedoImageViews[i] = state->albedoImageViews[i];
        vkrt->core.albedoImageMemories[i] = state->albedoImageMemories[i];
        vkrt->core.normalImages[i] = state->normalImages[i];
        vkrt->core.normalImageViews[i] = state->normalImageViews[i];
        vkrt->core.normalImageMemories[i] = state->normalImageMemories[i];
    }
    vkrt->core.selectionMaskImage = state->selectionMaskImage;
    vkrt->core.selectionMaskImageView = state->selectionMaskImageView;
    vkrt->core.selectionMaskImageMemory = state->selectionMaskImageMemory;

    vkrt->core.accumulationReadIndex = 0;
    vkrt->core.accumulationWriteIndex = 1;
    vkrt->renderStatus.accumulationFrame = 0;
    vkrt->renderStatus.totalSamples = 0;
    vkrt->core.accumulationNeedsReset = VK_TRUE;
    markSelectionMaskDirty(vkrt);
}

void destroyGPUImageState(VKRT* vkrt, GPUImageState* state) {
    if (!vkrt || !state || vkrt->core.device == VK_NULL_HANDLE) return;

    GPUImageSlot slots[8] = {0};
    uint32_t slotCount = queryGPUImageSlots(state, slots);

    for (uint32_t i = 0; i < slotCount; i++) {
        vkrtDestroyImageResources(vkrt, slots[i].image, slots[i].view, slots[i].memory);
    }
}

VKRT_Result createGPUImageState(VKRT* vkrt, VkExtent2D extent, GPUImageState* outState) {
    if (!vkrt || !outState) return VKRT_ERROR_INVALID_ARGUMENT;

    *outState = (GPUImageState){0};
    if (extent.width == 0 || extent.height == 0) {
        extent = vkrt->runtime.swapChainExtent;
    }
    if (extent.width == 0 || extent.height == 0) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    GPUImageSlot slots[8] = {0};
    uint32_t slotCount = queryGPUImageSlots(outState, slots);

    for (uint32_t i = 0; i < slotCount; i++) {
        if (vkrtCreateDeviceImage(
            vkrt,
            extent,
            slots[i].format,
            slots[i].usage,
            slots[i].image,
            slots[i].view,
            slots[i].memory
        ) != VKRT_SUCCESS) {
            LOG_ERROR("Failed to create image, #%d", i);
            destroyGPUImageState(vkrt, outState);
            return VKRT_ERROR_OPERATION_FAILED;
        }
    }

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (beginSingleTimeCommands(vkrt, &commandBuffer) != VKRT_SUCCESS) {
        destroyGPUImageState(vkrt, outState);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    for (uint32_t i = 0; i < slotCount; i++) {
        transitionImageLayout(commandBuffer, *slots[i].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    }

    if (endSingleTimeCommands(vkrt, commandBuffer) != VKRT_SUCCESS) {
        destroyGPUImageState(vkrt, outState);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    return VKRT_SUCCESS;
}

VKRT_Result createGPUImages(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkExtent2D renderExtent = vkrt->runtime.renderExtent;
    if (renderExtent.width == 0 || renderExtent.height == 0) {
        renderExtent = vkrt->runtime.swapChainExtent;
    }
    if (renderExtent.width == 0 || renderExtent.height == 0) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    GPUImageState newState = {0};
    VKRT_Result result = createGPUImageState(vkrt, renderExtent, &newState);
    if (result != VKRT_SUCCESS) {
        return result;
    }

    destroyGPUImages(vkrt);
    applyGPUImageState(vkrt, &newState);
    vkrt->runtime.renderExtent = renderExtent;
    return VKRT_SUCCESS;
}

void destroyGPUImages(VKRT* vkrt) {
    if (!vkrt) return;

    GPUImageState state = {0};
    captureGPUImageState(vkrt, &state);
    clearGPUImageBindings(vkrt);
    destroyGPUImageState(vkrt, &state);
}
