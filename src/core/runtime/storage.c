#include "storage.h"

#include "command/pool.h"
#include "command/record.h"
#include "device.h"
#include "debug.h"

#include <stdlib.h>

void destroyStorageImage(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    for (uint32_t i = 0; i < 2; i++) {
        if (vkrt->core.accumulationImageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(vkrt->core.device, vkrt->core.accumulationImageViews[i], NULL);
            vkrt->core.accumulationImageViews[i] = VK_NULL_HANDLE;
        }
        if (vkrt->core.accumulationImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(vkrt->core.device, vkrt->core.accumulationImages[i], NULL);
            vkrt->core.accumulationImages[i] = VK_NULL_HANDLE;
        }
        if (vkrt->core.accumulationImageMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(vkrt->core.device, vkrt->core.accumulationImageMemories[i], NULL);
            vkrt->core.accumulationImageMemories[i] = VK_NULL_HANDLE;
        }
    }

    if (vkrt->core.storageImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(vkrt->core.device, vkrt->core.storageImageView, NULL);
        vkrt->core.storageImageView = VK_NULL_HANDLE;
    }
    if (vkrt->core.storageImage != VK_NULL_HANDLE) {
        vkDestroyImage(vkrt->core.device, vkrt->core.storageImage, NULL);
        vkrt->core.storageImage = VK_NULL_HANDLE;
    }
    if (vkrt->core.storageImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, vkrt->core.storageImageMemory, NULL);
        vkrt->core.storageImageMemory = VK_NULL_HANDLE;
    }
}

VKRT_Result createStorageImage(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    const VkFormat accumulationFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
    const VkFormat outputFormat = VK_FORMAT_R16G16B16A16_UNORM;
    VkExtent2D renderExtent = vkrt->runtime.renderExtent;
    if (renderExtent.width == 0 || renderExtent.height == 0) {
        renderExtent = vkrt->runtime.swapChainExtent;
        vkrt->runtime.renderExtent = renderExtent;
    }

    VkImageCreateInfo imageCreateInfo = {0};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = accumulationFormat;
    imageCreateInfo.extent.width = renderExtent.width;
    imageCreateInfo.extent.height = renderExtent.height;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    for (uint32_t i = 0; i < 2; i++) {
        if (vkCreateImage(vkrt->core.device, &imageCreateInfo, NULL, &vkrt->core.accumulationImages[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create accumulation image");
            destroyStorageImage(vkrt);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        VkMemoryRequirements memoryRequirements;
        vkGetImageMemoryRequirements(vkrt->core.device, vkrt->core.accumulationImages[i], &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = {0};
        memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.allocationSize = memoryRequirements.size;
        if (findMemoryType(
                vkrt,
                memoryRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                &memoryAllocateInfo.memoryTypeIndex) != VKRT_SUCCESS) {
            destroyStorageImage(vkrt);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        if (vkAllocateMemory(vkrt->core.device, &memoryAllocateInfo, NULL, &vkrt->core.accumulationImageMemories[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to allocate accumulation image memory");
            destroyStorageImage(vkrt);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        if (vkBindImageMemory(vkrt->core.device, vkrt->core.accumulationImages[i], vkrt->core.accumulationImageMemories[i], 0) != VK_SUCCESS) {
            LOG_ERROR("Failed to bind accumulation image memory");
            destroyStorageImage(vkrt);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        VkImageViewCreateInfo imageViewCreateInfo = {0};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = accumulationFormat;
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;
        imageViewCreateInfo.image = vkrt->core.accumulationImages[i];

        if (vkCreateImageView(vkrt->core.device, &imageViewCreateInfo, NULL, &vkrt->core.accumulationImageViews[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create accumulation image view");
            destroyStorageImage(vkrt);
            return VKRT_ERROR_OPERATION_FAILED;
        }
    }

    vkrt->core.accumulationReadIndex = 0;
    vkrt->core.accumulationWriteIndex = 1;
    vkrt->core.accumulationNeedsReset = VK_TRUE;

    imageCreateInfo.format = outputFormat;
    if (vkCreateImage(vkrt->core.device, &imageCreateInfo, NULL, &vkrt->core.storageImage) != VK_SUCCESS) {
        LOG_ERROR("Failed to create output image");
        destroyStorageImage(vkrt);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkMemoryRequirements outputMemoryRequirements;
    vkGetImageMemoryRequirements(vkrt->core.device, vkrt->core.storageImage, &outputMemoryRequirements);

    VkMemoryAllocateInfo outputMemoryAllocateInfo = {0};
    outputMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    outputMemoryAllocateInfo.allocationSize = outputMemoryRequirements.size;
    if (findMemoryType(
            vkrt,
            outputMemoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &outputMemoryAllocateInfo.memoryTypeIndex) != VKRT_SUCCESS) {
        destroyStorageImage(vkrt);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkAllocateMemory(vkrt->core.device, &outputMemoryAllocateInfo, NULL, &vkrt->core.storageImageMemory) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate output image memory");
        destroyStorageImage(vkrt);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (vkBindImageMemory(vkrt->core.device, vkrt->core.storageImage, vkrt->core.storageImageMemory, 0) != VK_SUCCESS) {
        LOG_ERROR("Failed to bind output image memory");
        destroyStorageImage(vkrt);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkImageViewCreateInfo outputImageViewCreateInfo = {0};
    outputImageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    outputImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    outputImageViewCreateInfo.format = outputFormat;
    outputImageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    outputImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    outputImageViewCreateInfo.subresourceRange.levelCount = 1;
    outputImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    outputImageViewCreateInfo.subresourceRange.layerCount = 1;
    outputImageViewCreateInfo.image = vkrt->core.storageImage;

    if (vkCreateImageView(vkrt->core.device, &outputImageViewCreateInfo, NULL, &vkrt->core.storageImageView) != VK_SUCCESS) {
        LOG_ERROR("Failed to create output image view");
        destroyStorageImage(vkrt);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (beginSingleTimeCommands(vkrt, &commandBuffer) != VKRT_SUCCESS) {
        destroyStorageImage(vkrt);
        return VKRT_ERROR_OPERATION_FAILED;
    }
    transitionImageLayout(commandBuffer, vkrt->core.accumulationImages[0], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    transitionImageLayout(commandBuffer, vkrt->core.accumulationImages[1], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    transitionImageLayout(commandBuffer, vkrt->core.storageImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    if (endSingleTimeCommands(vkrt, commandBuffer) != VKRT_SUCCESS) {
        destroyStorageImage(vkrt);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    return VKRT_SUCCESS;
}
