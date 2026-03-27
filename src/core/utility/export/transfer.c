#include "buffer.h"
#include "command/pool.h"
#include "command/record.h"
#include "debug.h"
#include "internal.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int readbackImagePixels(
    VKRT* vkrt,
    VkImage image,
    uint32_t width,
    uint32_t height,
    RenderImageBufferFormat format,
    void** outPixels
) {
    if (outPixels) *outPixels = NULL;
    if (!vkrt || image == VK_NULL_HANDLE || width == 0u || height == 0u || !outPixels) return -1;

    size_t readbackBytes = 0u;
    if (!queryRenderImageBufferByteCount(width, height, format, &readbackBytes)) {
        LOG_ERROR("Render readback size overflow");
        return -1;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* mapped = NULL;
    int result = -1;

    if (createBuffer(
            vkrt,
            (VkDeviceSize)readbackBytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
            &stagingMemory
        ) != VKRT_SUCCESS) {
        return -1;
    }

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (beginSingleTimeCommands(vkrt, &commandBuffer) != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
        return -1;
    }

    transitionImageLayout(commandBuffer, image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy copyRegion = {0};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = (VkOffset3D){0, 0, 0};
    copyRegion.imageExtent = (VkExtent3D){width, height, 1};

    vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &copyRegion);
    transitionImageLayout(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

    if (endSingleTimeCommands(vkrt, commandBuffer) != VKRT_SUCCESS) {
        vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
        vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
        return -1;
    }

    if (vkMapMemory(vkrt->core.device, stagingMemory, 0, (VkDeviceSize)readbackBytes, 0, &mapped) != VK_SUCCESS) {
        LOG_ERROR("Failed to map render readback buffer");
        goto cleanup;
    }

    void* readbackCopy = malloc(readbackBytes);
    if (!readbackCopy) {
        LOG_ERROR("Failed to allocate render snapshot buffer");
        goto cleanup;
    }
    memcpy(readbackCopy, mapped, readbackBytes);
    *outPixels = readbackCopy;
    result = 0;

cleanup:
    if (mapped) {
        vkUnmapMemory(vkrt->core.device, stagingMemory);
    }
    vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
    vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
    return result;
}

int uploadImagePixels(
    VKRT* vkrt,
    VkImage image,
    uint32_t width,
    uint32_t height,
    const void* pixels,
    size_t byteCount
) {
    if (!vkrt || image == VK_NULL_HANDLE || width == 0u || height == 0u || !pixels || byteCount == 0u) return -1;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* mapped = NULL;
    int result = -1;

    if (createBuffer(
            vkrt,
            (VkDeviceSize)byteCount,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer,
            &stagingMemory
        ) != VKRT_SUCCESS) {
        return -1;
    }

    if (vkMapMemory(vkrt->core.device, stagingMemory, 0, (VkDeviceSize)byteCount, 0, &mapped) != VK_SUCCESS ||
        !mapped) {
        LOG_ERROR("Failed to map viewport upload staging buffer");
        goto cleanup;
    }
    memcpy(mapped, pixels, byteCount);
    vkUnmapMemory(vkrt->core.device, stagingMemory);
    mapped = NULL;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (beginSingleTimeCommands(vkrt, &commandBuffer) != VKRT_SUCCESS) {
        goto cleanup;
    }

    transitionImageLayout(commandBuffer, image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy copyRegion = {0};
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = (VkOffset3D){0, 0, 0};
    copyRegion.imageExtent = (VkExtent3D){width, height, 1u};

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    transitionImageLayout(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

    if (endSingleTimeCommands(vkrt, commandBuffer) != VKRT_SUCCESS) {
        goto cleanup;
    }

    result = 0;

cleanup:
    if (mapped) {
        vkUnmapMemory(vkrt->core.device, stagingMemory);
    }
    vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
    vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
    return result;
}
