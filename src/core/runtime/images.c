#include "images.h"

#include "command/pool.h"
#include "command/record.h"
#include "device.h"
#include "scene.h"
#include "debug.h"

typedef struct GPUImageSlot {
    VkImage* image;
    VkImageView* view;
    VkDeviceMemory* memory;
    VkFormat format;
    VkImageUsageFlags usage;
} GPUImageSlot;

static uint32_t queryGPUImageSlots(VKRT* vkrt, GPUImageSlot slots[4]) {
    if (!vkrt || !slots) return 0;

    slots[0] = (GPUImageSlot){
        .image = &vkrt->core.accumulationImages[0],
        .view = &vkrt->core.accumulationImageViews[0],
        .memory = &vkrt->core.accumulationImageMemories[0],
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    slots[1] = (GPUImageSlot){
        .image = &vkrt->core.accumulationImages[1],
        .view = &vkrt->core.accumulationImageViews[1],
        .memory = &vkrt->core.accumulationImageMemories[1],
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    slots[2] = (GPUImageSlot){
        .image = &vkrt->core.outputImage,
        .view = &vkrt->core.outputImageView,
        .memory = &vkrt->core.outputImageMemory,
        .format = VK_FORMAT_R16G16B16A16_UNORM,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    slots[3] = (GPUImageSlot){
        .image = &vkrt->core.selectionMaskImage,
        .view = &vkrt->core.selectionMaskImageView,
        .memory = &vkrt->core.selectionMaskImageMemory,
        .format = VK_FORMAT_R32_UINT,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    return 4;
}

VKRT_Result createGPUImages(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkExtent2D renderExtent = vkrt->runtime.renderExtent;
    if (renderExtent.width == 0 || renderExtent.height == 0) {
        renderExtent = vkrt->runtime.swapChainExtent;
        vkrt->runtime.renderExtent = renderExtent;
    }

    GPUImageSlot slots[4] = {0};
    uint32_t slotCount = queryGPUImageSlots(vkrt, slots);

    VkImageCreateInfo imageCreateInfo = {0};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.extent.width = renderExtent.width;
    imageCreateInfo.extent.height = renderExtent.height;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageViewCreateInfo imageViewCreateInfo = {0};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = 1;

    for (uint32_t i = 0; i < slotCount; i++) {
        imageCreateInfo.format = slots[i].format;
        imageCreateInfo.usage = slots[i].usage;
        if (vkCreateImage(vkrt->core.device, &imageCreateInfo, NULL, slots[i].image) != VK_SUCCESS) {
            LOG_ERROR("Failed to create image, #%d", i);
            destroyGPUImages(vkrt);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        VkMemoryRequirements memoryRequirements;
        vkGetImageMemoryRequirements(vkrt->core.device, *slots[i].image, &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = {0};
        memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.allocationSize = memoryRequirements.size;
        if (findMemoryType(vkrt, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memoryAllocateInfo.memoryTypeIndex) != VKRT_SUCCESS) {
            destroyGPUImages(vkrt);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        if (vkAllocateMemory(vkrt->core.device, &memoryAllocateInfo, NULL, slots[i].memory) != VK_SUCCESS) {
            LOG_ERROR("Failed to allocate image memory, #%d", i);
            destroyGPUImages(vkrt);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        if (vkBindImageMemory(vkrt->core.device, *slots[i].image, *slots[i].memory, 0) != VK_SUCCESS) {
            LOG_ERROR("Failed to bind image memory, #%d", i);
            destroyGPUImages(vkrt);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        imageViewCreateInfo.image = *slots[i].image;
        imageViewCreateInfo.format = slots[i].format;
        if (vkCreateImageView(vkrt->core.device, &imageViewCreateInfo, NULL, slots[i].view) != VK_SUCCESS) {
            LOG_ERROR("Failed to create image view, #%d", i);
            destroyGPUImages(vkrt);
            return VKRT_ERROR_OPERATION_FAILED;
        }
    }

    vkrt->core.accumulationReadIndex = 0;
    vkrt->core.accumulationWriteIndex = 1;
    vkrt->core.accumulationNeedsReset = VK_TRUE;
    markSelectionMaskDirty(vkrt);

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (beginSingleTimeCommands(vkrt, &commandBuffer) != VKRT_SUCCESS) {
        destroyGPUImages(vkrt);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    for (uint32_t i = 0; i < slotCount; i++) {
        transitionImageLayout(commandBuffer, *slots[i].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    }

    if (endSingleTimeCommands(vkrt, commandBuffer) != VKRT_SUCCESS) {
        destroyGPUImages(vkrt);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    return VKRT_SUCCESS;
}

void destroyGPUImages(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    GPUImageSlot slots[4] = {0};
    uint32_t slotCount = queryGPUImageSlots(vkrt, slots);

    for (uint32_t i = 0; i < slotCount; i++) {
        if (*slots[i].view != VK_NULL_HANDLE) {
            vkDestroyImageView(vkrt->core.device, *slots[i].view, NULL);
            *slots[i].view = VK_NULL_HANDLE;
        }

        if (*slots[i].image != VK_NULL_HANDLE) {
            vkDestroyImage(vkrt->core.device, *slots[i].image, NULL);
            *slots[i].image = VK_NULL_HANDLE;
        }

        if (*slots[i].memory != VK_NULL_HANDLE) {
            vkFreeMemory(vkrt->core.device, *slots[i].memory, NULL);
            *slots[i].memory = VK_NULL_HANDLE;
        }
    }
}
