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

static void clearGPUImageBindings(VKRT* vkrt) {
    if (!vkrt) return;

    vkrt->core.outputImage = VK_NULL_HANDLE;
    vkrt->core.outputImageView = VK_NULL_HANDLE;
    vkrt->core.outputImageMemory = VK_NULL_HANDLE;

    for (uint32_t i = 0; i < 2; i++) {
        vkrt->core.accumulationImages[i] = VK_NULL_HANDLE;
        vkrt->core.accumulationImageViews[i] = VK_NULL_HANDLE;
        vkrt->core.accumulationImageMemories[i] = VK_NULL_HANDLE;
    }

    vkrt->core.selectionMaskImage = VK_NULL_HANDLE;
    vkrt->core.selectionMaskImageView = VK_NULL_HANDLE;
    vkrt->core.selectionMaskImageMemory = VK_NULL_HANDLE;
}

static uint32_t queryGPUImageSlots(GPUImageState* state, GPUImageSlot slots[4]) {
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
        .image = &state->outputImage,
        .view = &state->outputImageView,
        .memory = &state->outputImageMemory,
        .format = VK_FORMAT_R16G16B16A16_UNORM,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    slots[3] = (GPUImageSlot){
        .image = &state->selectionMaskImage,
        .view = &state->selectionMaskImageView,
        .memory = &state->selectionMaskImageMemory,
        .format = VK_FORMAT_R32_UINT,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
    };
    return 4;
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

    GPUImageSlot slots[4] = {0};
    uint32_t slotCount = queryGPUImageSlots(state, slots);

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

VKRT_Result createGPUImageState(VKRT* vkrt, VkExtent2D extent, GPUImageState* outState) {
    if (!vkrt || !outState) return VKRT_ERROR_INVALID_ARGUMENT;

    *outState = (GPUImageState){0};
    if (extent.width == 0 || extent.height == 0) {
        extent = vkrt->runtime.swapChainExtent;
    }
    if (extent.width == 0 || extent.height == 0) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    GPUImageSlot slots[4] = {0};
    uint32_t slotCount = queryGPUImageSlots(outState, slots);

    VkImageCreateInfo imageCreateInfo = {0};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.extent.width = extent.width;
    imageCreateInfo.extent.height = extent.height;
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
            destroyGPUImageState(vkrt, outState);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        VkMemoryRequirements memoryRequirements = {0};
        vkGetImageMemoryRequirements(vkrt->core.device, *slots[i].image, &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = {0};
        memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.allocationSize = memoryRequirements.size;
        if (findMemoryType(
            vkrt,
            memoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &memoryAllocateInfo.memoryTypeIndex
        ) != VKRT_SUCCESS) {
            destroyGPUImageState(vkrt, outState);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        if (vkAllocateMemory(vkrt->core.device, &memoryAllocateInfo, NULL, slots[i].memory) != VK_SUCCESS) {
            LOG_ERROR("Failed to allocate image memory, #%d", i);
            destroyGPUImageState(vkrt, outState);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        if (vkBindImageMemory(vkrt->core.device, *slots[i].image, *slots[i].memory, 0) != VK_SUCCESS) {
            LOG_ERROR("Failed to bind image memory, #%d", i);
            destroyGPUImageState(vkrt, outState);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        imageViewCreateInfo.image = *slots[i].image;
        imageViewCreateInfo.format = slots[i].format;
        if (vkCreateImageView(vkrt->core.device, &imageViewCreateInfo, NULL, slots[i].view) != VK_SUCCESS) {
            LOG_ERROR("Failed to create image view, #%d", i);
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
