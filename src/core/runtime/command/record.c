#include "record.h"
#include "accel/accel.h"
#include "debug.h"
#include "view.h"

#include <stdlib.h>

static const VkClearColorValue kViewportClearColor = {.float32 = {0.02f, 0.02f, 0.025f, 1.0f}};

static void recordImageAccessBarrier(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkAccessFlags srcAccessMask,
    VkAccessFlags dstAccessMask,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask
) {
    VkImageMemoryBarrier barrier = {0};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;

    vkCmdPipelineBarrier(commandBuffer,
        srcStageMask,
        dstStageMask,
        0,
        0, NULL,
        0, NULL,
        1, &barrier);
}

static void recordSceneUpdateCommands(VKRT* vkrt, VkCommandBuffer commandBuffer) {
    FrameSceneUpdate* update = vkrtCurrentFrameSceneUpdate(vkrt);
    VkBool32 hasTransferWrites = VK_FALSE;
    VkBool32 hasBLASBuilds = update->blasBuildCount > 0 ? VK_TRUE : VK_FALSE;
    VkBool32 hasTLASBuild = update->tlasBuildPending;

    for (uint32_t i = 0; i < update->sceneTransferCount; i++) {
        PendingBufferCopy* transfer = &update->sceneTransfers[i];
        VkBufferCopy copyRegion = {
            .size = transfer->size,
        };
        vkCmdCopyBuffer(commandBuffer, transfer->stagingBuffer, transfer->dstBuffer, 1, &copyRegion);
        hasTransferWrites = VK_TRUE;
    }

    for (uint32_t i = 0; i < update->geometryUploadCount; i++) {
        PendingGeometryUpload* upload = &update->geometryUploads[i];
        Mesh* mesh = &vkrt->core.meshes[upload->meshIndex];
        VkBufferCopy copyRegions[2] = {
            {
                .srcOffset = 0,
                .dstOffset = (VkDeviceSize)mesh->info.vertexBase * sizeof(Vertex),
                .size = (VkDeviceSize)mesh->info.vertexCount * sizeof(Vertex),
            },
            {
                .srcOffset = upload->indexOffset,
                .dstOffset = (VkDeviceSize)mesh->info.indexBase * sizeof(uint32_t),
                .size = (VkDeviceSize)mesh->info.indexCount * sizeof(uint32_t),
            },
        };
        vkCmdCopyBuffer(commandBuffer, upload->stagingBuffer, vkrt->core.vertexData.buffer, 1, &copyRegions[0]);
        vkCmdCopyBuffer(commandBuffer, upload->stagingBuffer, vkrt->core.indexData.buffer, 1, &copyRegions[1]);
        hasTransferWrites = VK_TRUE;
    }


    if (hasTransferWrites) {
        VkMemoryBarrier transferBarrier = {0};
        transferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        transferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        transferBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0,
            1,
            &transferBarrier,
            0,
            NULL,
            0,
            NULL);
    }

    if (hasBLASBuilds) {
        recordBottomLevelAccelerationStructureBuilds(vkrt, commandBuffer);

        VkMemoryBarrier blasBarrier = {0};
        blasBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        blasBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        blasBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0,
            1,
            &blasBarrier,
            0,
            NULL,
            0,
            NULL);
    }

    if (hasTLASBuild) {
        recordTopLevelAccelerationStructureBuild(vkrt, commandBuffer);
    }

    if (hasTransferWrites || hasBLASBuilds || hasTLASBuild) {
        VkMemoryBarrier sceneReadyBarrier = {0};
        sceneReadyBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        sceneReadyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        sceneReadyBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0,
            1,
            &sceneReadyBarrier,
            0,
            NULL,
            0,
            NULL);
    }
}

VKRT_Result recordCommandBuffer(VKRT* vkrt, uint32_t imageIndex) {
    if (!vkrt || imageIndex >= vkrt->runtime.swapChainImageCount) return VKRT_ERROR_INVALID_ARGUMENT;

    VkCommandBuffer commandBuffer = vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame];
    VkExtent2D swapchainExtent = vkrt->runtime.swapChainExtent;
    VkExtent2D renderExtent = vkrt->runtime.renderExtent;
    if (renderExtent.width == 0 || renderExtent.height == 0) {
        renderExtent = swapchainExtent;
    }
    VkImage accumulationReadImage = vkrt->core.accumulationImages[vkrt->core.accumulationReadIndex];
    VkImage accumulationWriteImage = vkrt->core.accumulationImages[vkrt->core.accumulationWriteIndex];
    VkImage outputImage = vkrt->core.outputImage;
    VkImage selectionMaskImage = vkrt->core.selectionMaskImage;
    VkImage destImage = vkrt->runtime.swapChainImages[imageIndex];

    VkCommandBufferBeginInfo commandBufferBeginInfo = {0};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
        LOG_ERROR("Failed to begin command buffer");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint32_t qbase = vkrt->runtime.currentFrame * 2;
    vkCmdResetQueryPool(commandBuffer, vkrt->runtime.timestampPool, qbase, 2);

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vkrt->runtime.timestampPool, qbase);
    recordSceneUpdateCommands(vkrt, commandBuffer);

    transitionImageLayout(commandBuffer, destImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageSubresourceRange clearRange = {0};
    clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearRange.baseMipLevel = 0;
    clearRange.levelCount = 1;
    clearRange.baseArrayLayer = 0;
    clearRange.layerCount = 1;

    VkBool32 renderModeActive = vkrt->state.renderModeActive != 0;
    VkBool32 renderFinished = renderModeActive && vkrt->state.renderModeFinished;
    VkBool32 descriptorReady = vkrt->core.descriptorSetReady[vkrt->runtime.currentFrame];
    VkBool32 shouldTrace = descriptorReady && !renderFinished;
    VkBool32 selectionOverlayEnabled = !renderModeActive &&
                                       (vkrt->state.selectionEnabled != 0u ||
                                        vkrt->state.debugMode == VKRT_DEBUG_MODE_SELECTION_MASK);
    VkBool32 shouldSelectionTrace = shouldTrace &&
                                    selectionOverlayEnabled &&
                                    vkrt->core.selectionMaskDirty &&
                                    vkrt->core.selectionRayTracingPipeline != VK_NULL_HANDLE;
    VkBool32 shouldSelectionPost = shouldTrace &&
                                   selectionOverlayEnabled &&
                                   vkrt->core.computePipeline != VK_NULL_HANDLE;
    vkrt->runtime.frameTraced = VK_FALSE;
    vkrt->runtime.frameSelectionTraced = VK_FALSE;

    if (shouldTrace) {
        vkrt->runtime.frameTraced = VK_TRUE;
    }

    if (descriptorReady) {
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
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vkrt->core.pipelineLayout, 0, 1,
                                   &vkrt->core.descriptorSets[vkrt->runtime.currentFrame], 0, NULL);
            vkrt->core.procs.vkCmdTraceRaysKHR(commandBuffer, &vkrt->core.shaderBindingTables[0], &vkrt->core.shaderBindingTables[1],
                                               &vkrt->core.shaderBindingTables[2], &vkrt->core.shaderBindingTables[3],
                                               renderExtent.width, renderExtent.height, 1);

            VkImageMemoryBarrier accumulationBarrier = {0};
            accumulationBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            accumulationBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            accumulationBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            accumulationBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            accumulationBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            accumulationBarrier.image = accumulationWriteImage;
            accumulationBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            accumulationBarrier.subresourceRange.baseMipLevel = 0;
            accumulationBarrier.subresourceRange.levelCount = 1;
            accumulationBarrier.subresourceRange.baseArrayLayer = 0;
            accumulationBarrier.subresourceRange.layerCount = 1;
            accumulationBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            accumulationBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                0,
                0,
                NULL,
                0,
                NULL,
                1,
                &accumulationBarrier);

            if (shouldSelectionTrace) {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vkrt->core.selectionRayTracingPipeline);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vkrt->core.pipelineLayout, 0, 1,
                                       &vkrt->core.descriptorSets[vkrt->runtime.currentFrame], 0, NULL);
                vkrt->core.procs.vkCmdTraceRaysKHR(commandBuffer, &vkrt->core.selectionShaderBindingTables[0], &vkrt->core.selectionShaderBindingTables[1],
                                                   &vkrt->core.selectionShaderBindingTables[2], &vkrt->core.selectionShaderBindingTables[3],
                                                   renderExtent.width, renderExtent.height, 1);
                vkrt->runtime.frameSelectionTraced = VK_TRUE;
            }

            if (shouldSelectionPost) {
                if (shouldSelectionTrace) {
                    recordImageAccessBarrier(
                        commandBuffer,
                        selectionMaskImage,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                }
                recordImageAccessBarrier(
                    commandBuffer,
                    outputImage,
                    VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vkrt->core.computePipeline);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vkrt->core.pipelineLayout, 0, 1,
                    &vkrt->core.descriptorSets[vkrt->runtime.currentFrame], 0, NULL);

                const uint32_t localSizeX = 16u;
                const uint32_t localSizeY = 16u;
                uint32_t groupCountX = (renderExtent.width + localSizeX - 1u) / localSizeX;
                uint32_t groupCountY = (renderExtent.height + localSizeY - 1u) / localSizeY;
                vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);

                recordImageAccessBarrier(
                    commandBuffer,
                    outputImage,
                    VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);
            }
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

            vkrtClampViewportRect(swapchainExtent, &x, &y, &width, &height);
            vkCmdClearColorImage(commandBuffer, destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &kViewportClearColor, 1, &clearRange);

            VkExtent2D viewportExtent = {width, height};
            vkrtQueryRenderViewCropExtent(renderExtent, viewportExtent, vkrt->state.renderViewZoom, &srcWidth, &srcHeight, &fillViewport);

            int32_t maxSrcX = (int32_t)renderExtent.width - (int32_t)srcWidth;
            int32_t maxSrcY = (int32_t)renderExtent.height - (int32_t)srcHeight;
            float panX = vkrt->state.renderViewPanX;
            float panY = vkrt->state.renderViewPanY;
            vkrtClampRenderViewPanOffset(renderExtent, viewportExtent, vkrt->state.renderViewZoom, &panX, &panY);
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

        vkCmdBlitImage(commandBuffer, outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destImage,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
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

    if (descriptorReady) {
        transitionImageLayout(commandBuffer, outputImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vkrt->runtime.timestampPool, qbase + 1);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to end command buffer");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    return VKRT_SUCCESS;
}
