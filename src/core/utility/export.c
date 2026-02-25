#include "export.h"

#include "buffer.h"
#include "command.h"
#include "debug.h"

#define SPNG_STATIC
#include "spng.h"

#include <stdint.h>
#include <stdio.h>

static int writePNGFile(const char* path, const uint16_t* rgba16, uint32_t width, uint32_t height) {
    FILE* file = fopen(path, "wb");
    if (!file) {
        LOG_ERROR("Failed to open '%s' for PNG export", path);
        return -1;
    }

    spng_ctx* context = spng_ctx_new(SPNG_CTX_ENCODER);
    if (!context) {
        fclose(file);
        LOG_ERROR("Failed to create PNG encoder context");
        return -1;
    }

    struct spng_ihdr ihdr = {0};
    ihdr.width = width;
    ihdr.height = height;
    ihdr.bit_depth = 16;
    ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    ihdr.compression_method = 0;
    ihdr.filter_method = 0;
    ihdr.interlace_method = SPNG_INTERLACE_NONE;

    int err = spng_set_png_file(context, file);
    if (err == SPNG_OK) err = spng_set_ihdr(context, &ihdr);
    if (err == SPNG_OK) err = spng_set_option(context, SPNG_FILTER_CHOICE, SPNG_FILTER_CHOICE_ALL);
    if (err == SPNG_OK) {
        err = spng_encode_image(context, rgba16, (size_t)width * height * 4 * sizeof(uint16_t), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    }

    spng_ctx_free(context);
    fclose(file);

    if (err != SPNG_OK) {
        remove(path);
        LOG_ERROR("PNG export failed for '%s': %s", path, spng_strerror(err));
        return -1;
    }

    return 0;
}

int saveCurrentRenderPNG(VKRT* vkrt, const char* path) {
    if (!vkrt || !path || !path[0]) return -1;
    if (vkrt->core.storageImage == VK_NULL_HANDLE) {
        LOG_ERROR("Cannot save PNG before storage image is initialized");
        return -1;
    }

    uint32_t fullWidth = vkrt->runtime.swapChainExtent.width;
    uint32_t fullHeight = vkrt->runtime.swapChainExtent.height;
    if (fullWidth == 0 || fullHeight == 0) {
        LOG_ERROR("Cannot save PNG with invalid render size %ux%u", fullWidth, fullHeight);
        return -1;
    }

    uint32_t offsetX = 0;
    uint32_t offsetY = 0;
    uint32_t width = fullWidth;
    uint32_t height = fullHeight;

    if (vkrt->core.sceneData) {
        uint32_t* rect = vkrt->core.sceneData->viewportRect;
        if (rect[2] > 0 && rect[3] > 0) {
            offsetX = rect[0];
            offsetY = rect[1];
            width = rect[2];
            height = rect[3];

            if (offsetX >= fullWidth) offsetX = fullWidth - 1;
            if (offsetY >= fullHeight) offsetY = fullHeight - 1;
            if (offsetX + width > fullWidth) width = fullWidth - offsetX;
            if (offsetY + height > fullHeight) height = fullHeight - offsetY;

            if (width == 0 || height == 0) {
                offsetX = 0;
                offsetY = 0;
                width = fullWidth;
                height = fullHeight;
            }
        }
    }

    VkDeviceSize pixelCount = (VkDeviceSize)width * (VkDeviceSize)height;
    VkDeviceSize readbackBytes = pixelCount * 4 * sizeof(uint16_t);
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* mapped = NULL;
    int result = -1;

    vkDeviceWaitIdle(vkrt->core.device);

    createBuffer(vkrt,
        readbackBytes,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer,
        &stagingMemory);

    VkCommandBuffer commandBuffer = beginSingleTimeCommands(vkrt);
    transitionImageLayout(commandBuffer, vkrt->core.storageImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy copyRegion = {0};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;
    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageOffset = (VkOffset3D){(int32_t)offsetX, (int32_t)offsetY, 0};
    copyRegion.imageExtent = (VkExtent3D){width, height, 1};

    vkCmdCopyImageToBuffer(commandBuffer,
        vkrt->core.storageImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stagingBuffer,
        1,
        &copyRegion);

    transitionImageLayout(commandBuffer, vkrt->core.storageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    endSingleTimeCommands(vkrt, commandBuffer);

    if (vkMapMemory(vkrt->core.device, stagingMemory, 0, readbackBytes, 0, &mapped) == VK_SUCCESS) {
        result = writePNGFile(path, (const uint16_t*)mapped, width, height);
        if (result == 0) {
            LOG_INFO("Saved render PNG: %s", path);
        }
        vkUnmapMemory(vkrt->core.device, stagingMemory);
    } else {
        LOG_ERROR("Failed to map PNG readback buffer");
    }

    if (stagingBuffer != VK_NULL_HANDLE) vkDestroyBuffer(vkrt->core.device, stagingBuffer, NULL);
    if (stagingMemory != VK_NULL_HANDLE) vkFreeMemory(vkrt->core.device, stagingMemory, NULL);
    return result;
}
