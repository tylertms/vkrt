#include "command.h"
#include "device.h"
#include "debug.h"

#include <stdio.h>
#include <stdlib.h>

static float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static const VkClearColorValue kViewportClearColor = {.float32 = {0.02f, 0.02f, 0.025f, 1.0f}};

static void clampRectToExtent(uint32_t* x, uint32_t* y, uint32_t* width, uint32_t* height, VkExtent2D extent) {
    if (!x || !y || !width || !height) return;

    if (extent.width == 0 || extent.height == 0) {
        *x = 0;
        *y = 0;
        *width = 0;
        *height = 0;
        return;
    }

    if (*width == 0 || *height == 0) {
        *x = 0;
        *y = 0;
        *width = extent.width;
        *height = extent.height;
    }

    if (*x >= extent.width) *x = extent.width - 1;
    if (*y >= extent.height) *y = extent.height - 1;
    if (*x + *width > extent.width) *width = extent.width - *x;
    if (*y + *height > extent.height) *height = extent.height - *y;

    if (*width == 0 || *height == 0) {
        *x = 0;
        *y = 0;
        *width = extent.width;
        *height = extent.height;
    }
}

static void queryRenderViewCrop(VkExtent2D renderExtent, uint32_t viewportWidth, uint32_t viewportHeight, float zoom, uint32_t* outWidth, uint32_t* outHeight, VkBool32* outFillViewport) {
    if (!outWidth || !outHeight) return;

    float renderWidth = (float)renderExtent.width;
    float renderHeight = (float)renderExtent.height;
    float clampedZoom = clampFloat(zoom, VKRT_RENDER_VIEW_ZOOM_MIN, VKRT_RENDER_VIEW_ZOOM_MAX);
    VkBool32 fillViewport = (clampedZoom > (VKRT_RENDER_VIEW_ZOOM_MIN + 0.0001f)) &&
                            viewportWidth > 0 &&
                            viewportHeight > 0;

    uint32_t cropWidth = renderExtent.width;
    uint32_t cropHeight = renderExtent.height;
    if (fillViewport) {
        float renderAspect = renderWidth / renderHeight;
        float viewAspect = (float)viewportWidth / (float)viewportHeight;
        float baseWidth = renderWidth;
        float baseHeight = renderHeight;
        if (viewAspect > renderAspect) {
            baseHeight = renderWidth / viewAspect;
        } else {
            baseWidth = renderHeight * viewAspect;
        }

        float cropWidthF = baseWidth / clampedZoom;
        float cropHeightF = baseHeight / clampedZoom;
        if (cropWidthF < 1.0f) cropWidthF = 1.0f;
        if (cropHeightF < 1.0f) cropHeightF = 1.0f;
        if (cropWidthF > renderWidth) cropWidthF = renderWidth;
        if (cropHeightF > renderHeight) cropHeightF = renderHeight;

        cropWidth = (uint32_t)(cropWidthF + 0.5f);
        cropHeight = (uint32_t)(cropHeightF + 0.5f);
    }

    if (cropWidth < 1) cropWidth = 1;
    if (cropHeight < 1) cropHeight = 1;
    if (cropWidth > renderExtent.width) cropWidth = renderExtent.width;
    if (cropHeight > renderExtent.height) cropHeight = renderExtent.height;

    *outWidth = cropWidth;
    *outHeight = cropHeight;
    if (outFillViewport) *outFillViewport = fillViewport;
}

void createCommandPool(VKRT* vkrt) {
    QueueFamily indices = findQueueFamilies(vkrt);

    VkCommandPoolCreateInfo commandPoolCreateInfo = {0};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCreateInfo.queueFamilyIndex = indices.graphics;

    if (vkCreateCommandPool(vkrt->core.device, &commandPoolCreateInfo, NULL, &vkrt->runtime.commandPool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create command pool");
        exit(EXIT_FAILURE);
    }
}

void createCommandBuffers(VKRT* vkrt) {
    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {0};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool = vkrt->runtime.commandPool;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    if (vkAllocateCommandBuffers(vkrt->core.device, &commandBufferAllocateInfo, vkrt->runtime.commandBuffers) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate command buffers");
        exit(EXIT_FAILURE);
    }
}

void recordCommandBuffer(VKRT* vkrt, uint32_t imageIndex) {
    VkCommandBuffer commandBuffer = vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame];
    VkExtent2D swapchainExtent = vkrt->runtime.swapChainExtent;
    VkExtent2D renderExtent = vkrt->runtime.renderExtent;
    if (renderExtent.width == 0 || renderExtent.height == 0) {
        renderExtent = swapchainExtent;
    }
    VkImage accumulationReadImage = vkrt->core.accumulationImages[vkrt->core.accumulationReadIndex];
    VkImage accumulationWriteImage = vkrt->core.accumulationImages[vkrt->core.accumulationWriteIndex];
    VkImage outputImage = vkrt->core.storageImage;
    VkImage destImage = vkrt->runtime.swapChainImages[imageIndex];

    VkCommandBufferBeginInfo commandBufferBeginInfo = {0};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
        LOG_ERROR("Failed to begin command buffer");
        exit(EXIT_FAILURE);
    }

    uint32_t qbase = vkrt->runtime.currentFrame * 2;
    vkCmdResetQueryPool(commandBuffer, vkrt->runtime.timestampPool, qbase, 2);

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vkrt->runtime.timestampPool, qbase);

    transitionImageLayout(commandBuffer, destImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageSubresourceRange clearRange = {0};
    clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearRange.baseMipLevel = 0;
    clearRange.levelCount = 1;
    clearRange.baseArrayLayer = 0;
    clearRange.layerCount = 1;

    VkBool32 renderModeActive = vkrt->state.renderModeActive != 0;
    VkBool32 renderFinished = renderModeActive && vkrt->state.renderModeFinished;
    VkBool32 shouldTrace = vkrt->core.descriptorSetReady && !renderFinished;
    vkrt->runtime.frameTraced = VK_FALSE;

    if (shouldTrace) {
        vkrt->runtime.frameTraced = VK_TRUE;
    }

    if (vkrt->core.descriptorSetReady) {
        if (vkrt->core.accumulationNeedsReset) {
            vkrt->state.accumulationFrame = 0;
            vkrt->state.totalSamples = 0;
            VkClearColorValue clearZero = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}};
            transitionImageLayout(commandBuffer, accumulationReadImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            transitionImageLayout(commandBuffer, accumulationWriteImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            transitionImageLayout(commandBuffer, outputImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdClearColorImage(commandBuffer, accumulationReadImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearZero, 1, &clearRange);
            vkCmdClearColorImage(commandBuffer, accumulationWriteImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearZero, 1, &clearRange);
            vkCmdClearColorImage(commandBuffer, outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearZero, 1, &clearRange);
            transitionImageLayout(commandBuffer, accumulationReadImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
            transitionImageLayout(commandBuffer, accumulationWriteImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
            transitionImageLayout(commandBuffer, outputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
            vkrt->core.accumulationNeedsReset = VK_FALSE;
        }

        if (shouldTrace) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vkrt->core.rayTracingPipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vkrt->core.pipelineLayout, 0, 1, &vkrt->core.descriptorSet, 0, NULL);
            vkrt->core.procs.vkCmdTraceRaysKHR(commandBuffer, &vkrt->core.shaderBindingTables[0], &vkrt->core.shaderBindingTables[1], &vkrt->core.shaderBindingTables[2], &vkrt->core.shaderBindingTables[3], renderExtent.width, renderExtent.height, 1);
            transitionImageLayout(commandBuffer, accumulationWriteImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            transitionImageLayout(commandBuffer, accumulationReadImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        transitionImageLayout(commandBuffer, outputImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkImageBlit blit = {0};
        blit.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[0] = (VkOffset3D){0, 0, 0};
        blit.srcOffsets[1] = (VkOffset3D){(int32_t)renderExtent.width, (int32_t)renderExtent.height, 1};
        blit.dstSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};

        if (vkrt->state.renderModeActive) {
            uint32_t x = vkrt->runtime.displayViewportRect[0];
            uint32_t y = vkrt->runtime.displayViewportRect[1];
            uint32_t width = vkrt->runtime.displayViewportRect[2];
            uint32_t height = vkrt->runtime.displayViewportRect[3];
            uint32_t srcWidth = renderExtent.width;
            uint32_t srcHeight = renderExtent.height;
            VkBool32 fillViewport = VK_FALSE;
            int32_t srcX = 0;
            int32_t srcY = 0;

            clampRectToExtent(&x, &y, &width, &height, swapchainExtent);
            vkCmdClearColorImage(commandBuffer, destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &kViewportClearColor, 1, &clearRange);

            queryRenderViewCrop(renderExtent, width, height, vkrt->state.renderViewZoom, &srcWidth, &srcHeight, &fillViewport);

            int32_t maxSrcX = (int32_t)renderExtent.width - (int32_t)srcWidth;
            int32_t maxSrcY = (int32_t)renderExtent.height - (int32_t)srcHeight;
            float maxPanX = (float)maxSrcX * 0.5f;
            float maxPanY = (float)maxSrcY * 0.5f;
            float panX = clampFloat(vkrt->state.renderViewPanX, -maxPanX, maxPanX);
            float panY = clampFloat(vkrt->state.renderViewPanY, -maxPanY, maxPanY);
            if (maxSrcX > 0) srcX = (int32_t)((float)maxSrcX * 0.5f + panX + 0.5f);
            if (maxSrcY > 0) srcY = (int32_t)((float)maxSrcY * 0.5f + panY + 0.5f);
            if (srcX < 0) srcX = 0;
            if (srcY < 0) srcY = 0;
            if (srcX > maxSrcX) srcX = maxSrcX;
            if (srcY > maxSrcY) srcY = maxSrcY;

            blit.srcOffsets[0] = (VkOffset3D){srcX, srcY, 0};
            blit.srcOffsets[1] = (VkOffset3D){srcX + (int32_t)srcWidth, srcY + (int32_t)srcHeight, 1};

            if (fillViewport) {
                blit.dstOffsets[0] = (VkOffset3D){(int32_t)x, (int32_t)y, 0};
                blit.dstOffsets[1] = (VkOffset3D){(int32_t)(x + width), (int32_t)(y + height), 1};
            } else {
                float srcAspect = (float)srcWidth / (float)srcHeight;
                float dstAspect = (float)width / (float)height;
                uint32_t fitWidth = width;
                uint32_t fitHeight = height;
                if (dstAspect > srcAspect) {
                    fitWidth = (uint32_t)((float)height * srcAspect + 0.5f);
                    if (fitWidth == 0) fitWidth = 1;
                } else {
                    fitHeight = (uint32_t)((float)width / srcAspect + 0.5f);
                    if (fitHeight == 0) fitHeight = 1;
                }

                uint32_t dstX = x + (width - fitWidth) / 2;
                uint32_t dstY = y + (height - fitHeight) / 2;

                blit.dstOffsets[0] = (VkOffset3D){(int32_t)dstX, (int32_t)dstY, 0};
                blit.dstOffsets[1] = (VkOffset3D){(int32_t)(dstX + fitWidth), (int32_t)(dstY + fitHeight), 1};
            }
        } else {
            blit.dstOffsets[0] = (VkOffset3D){0, 0, 0};
            blit.dstOffsets[1] = (VkOffset3D){(int32_t)swapchainExtent.width, (int32_t)swapchainExtent.height, 1};
        }

        vkCmdBlitImage(commandBuffer, outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        if (shouldTrace) {
            VkImageCopy copyRegion = {0};
            copyRegion.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.extent = (VkExtent3D){renderExtent.width, renderExtent.height, 1};
            vkCmdCopyImage(commandBuffer, accumulationWriteImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, accumulationReadImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        }
    } else {
        vkCmdClearColorImage(commandBuffer, destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &kViewportClearColor, 1, &clearRange);
    }

    VkRenderPassBeginInfo renderPassBeginInfo = {0};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = vkrt->runtime.renderPass;
    renderPassBeginInfo.framebuffer = vkrt->runtime.framebuffers[imageIndex];
    renderPassBeginInfo.renderArea.offset = (VkOffset2D){0, 0};
    renderPassBeginInfo.renderArea.extent = swapchainExtent;
    renderPassBeginInfo.clearValueCount = 0;
    renderPassBeginInfo.pClearValues = NULL;

    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (vkrt->appHooks.drawOverlay) {
        vkrt->appHooks.drawOverlay(vkrt, commandBuffer, vkrt->appHooks.userData);
    }

    vkCmdEndRenderPass(commandBuffer);

    if (vkrt->core.descriptorSetReady) {
        if (shouldTrace) {
            transitionImageLayout(commandBuffer, accumulationReadImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
            transitionImageLayout(commandBuffer, accumulationWriteImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        }
        transitionImageLayout(commandBuffer, outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vkrt->runtime.timestampPool, qbase + 1);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to end command buffer");
        exit(EXIT_FAILURE);
    }
}

VkCommandBuffer beginSingleTimeCommands(VKRT* vkrt) {
    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {0};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandPool = vkrt->runtime.commandPool;
    commandBufferAllocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(vkrt->core.device, &commandBufferAllocateInfo, &commandBuffer);

    VkCommandBufferBeginInfo commandBufferBeginInfo = {0};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);

    return commandBuffer;
}

void endSingleTimeCommands(VKRT* vkrt, VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(vkrt->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vkrt->core.graphicsQueue);

    vkFreeCommandBuffers(vkrt->core.device, vkrt->runtime.commandPool, 1, &commandBuffer);
}

void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;

    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    } else {
        LOG_ERROR("Unsupported layout transition: %d -> %d", oldLayout, newLayout);
        exit(EXIT_FAILURE);
    }

    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

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

void createStorageImage(VKRT* vkrt) {
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
            exit(EXIT_FAILURE);
        }

        VkMemoryRequirements memoryRequirements;
        vkGetImageMemoryRequirements(vkrt->core.device, vkrt->core.accumulationImages[i], &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = {0};
        memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.allocationSize = memoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex = findMemoryType(vkrt, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(vkrt->core.device, &memoryAllocateInfo, NULL, &vkrt->core.accumulationImageMemories[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to allocate accumulation image memory");
            exit(EXIT_FAILURE);
        }

        if (vkBindImageMemory(vkrt->core.device, vkrt->core.accumulationImages[i], vkrt->core.accumulationImageMemories[i], 0) != VK_SUCCESS) {
            LOG_ERROR("Failed to bind accumulation image memory");
            exit(EXIT_FAILURE);
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
            exit(EXIT_FAILURE);
        }
    }

    vkrt->core.accumulationReadIndex = 0;
    vkrt->core.accumulationWriteIndex = 1;
    vkrt->core.accumulationNeedsReset = VK_TRUE;

    imageCreateInfo.format = outputFormat;
    if (vkCreateImage(vkrt->core.device, &imageCreateInfo, NULL, &vkrt->core.storageImage) != VK_SUCCESS) {
        LOG_ERROR("Failed to create output image");
        exit(EXIT_FAILURE);
    }

    VkMemoryRequirements outputMemoryRequirements;
    vkGetImageMemoryRequirements(vkrt->core.device, vkrt->core.storageImage, &outputMemoryRequirements);

    VkMemoryAllocateInfo outputMemoryAllocateInfo = {0};
    outputMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    outputMemoryAllocateInfo.allocationSize = outputMemoryRequirements.size;
    outputMemoryAllocateInfo.memoryTypeIndex = findMemoryType(vkrt, outputMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(vkrt->core.device, &outputMemoryAllocateInfo, NULL, &vkrt->core.storageImageMemory) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate output image memory");
        exit(EXIT_FAILURE);
    }

    if (vkBindImageMemory(vkrt->core.device, vkrt->core.storageImage, vkrt->core.storageImageMemory, 0) != VK_SUCCESS) {
        LOG_ERROR("Failed to bind output image memory");
        exit(EXIT_FAILURE);
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
        exit(EXIT_FAILURE);
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands(vkrt);
    transitionImageLayout(commandBuffer, vkrt->core.accumulationImages[0], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    transitionImageLayout(commandBuffer, vkrt->core.accumulationImages[1], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    transitionImageLayout(commandBuffer, vkrt->core.storageImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    endSingleTimeCommands(vkrt, commandBuffer);
}
