#include "command.h"
#include "buffer.h"
#include "descriptor.h"
#include "device.h"
#include "scene.h"
#include "structure.h"
#include "swapchain.h"

#include <stdio.h>
#include <stdlib.h>

void createCommandPool(VKRT* vkrt) {
    QueueFamily indices = findQueueFamilies(vkrt);

    VkCommandPoolCreateInfo commandPoolCreateInfo = {0};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCreateInfo.queueFamilyIndex = indices.graphics;

    if (vkCreateCommandPool(vkrt->core.device, &commandPoolCreateInfo, NULL, &vkrt->runtime.commandPool) != VK_SUCCESS) {
        perror("[ERROR]: Failed to create command pool");
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
        perror("[ERROR]: Failed to allocate command buffers");
        exit(EXIT_FAILURE);
    }
}

void recordCommandBuffer(VKRT* vkrt, uint32_t imageIndex) {
    VkCommandBuffer commandBuffer = vkrt->runtime.commandBuffers[vkrt->runtime.currentFrame];
    VkExtent2D extent = vkrt->runtime.swapChainExtent;
    VkImage accumulationReadImage = vkrt->core.accumulationImages[vkrt->core.accumulationReadIndex];
    VkImage accumulationWriteImage = vkrt->core.accumulationImages[vkrt->core.accumulationWriteIndex];
    VkImage destImage = vkrt->runtime.swapChainImages[imageIndex];

    VkCommandBufferBeginInfo commandBufferBeginInfo = {0};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS) {
        perror("[ERROR]: Failed to begin command buffer");
        exit(EXIT_FAILURE);
    }

    uint32_t qbase = vkrt->runtime.currentFrame * 2;
    vkCmdResetQueryPool(commandBuffer, vkrt->runtime.timestampPool, qbase, 2);

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vkrt->runtime.timestampPool, qbase);

    transitionImageLayout(commandBuffer, destImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    if (vkrt->core.descriptorSetReady) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vkrt->core.rayTracingPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vkrt->core.pipelineLayout, 0, 1, &vkrt->core.descriptorSet, 0, NULL);

        PFN_vkCmdTraceRaysKHR pvkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(vkrt->core.device, "vkCmdTraceRaysKHR");
        pvkCmdTraceRaysKHR(commandBuffer, &vkrt->core.shaderBindingTables[0], &vkrt->core.shaderBindingTables[1], &vkrt->core.shaderBindingTables[2], &vkrt->core.shaderBindingTables[3], extent.width, extent.height, 1);

        transitionImageLayout(commandBuffer, accumulationWriteImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        transitionImageLayout(commandBuffer, accumulationReadImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageBlit blit = (VkImageBlit){0};
        blit.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[0] = (VkOffset3D){0, 0, 0};
        blit.srcOffsets[1] = (VkOffset3D){(int32_t)extent.width, (int32_t)extent.height, 1};
        blit.dstSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstOffsets[0] = (VkOffset3D){0, 0, 0};
        blit.dstOffsets[1] = (VkOffset3D){(int32_t)extent.width, (int32_t)extent.height, 1};

        vkCmdBlitImage(commandBuffer, accumulationWriteImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        VkImageCopy copyRegion = {0};
        copyRegion.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.extent = (VkExtent3D){extent.width, extent.height, 1};
        vkCmdCopyImage(commandBuffer, accumulationWriteImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, accumulationReadImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    } else {
        VkClearColorValue clearColor = {.float32 = {0.02f, 0.02f, 0.025f, 1.0f}};
        VkImageSubresourceRange clearRange = {0};
        clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearRange.baseMipLevel = 0;
        clearRange.levelCount = 1;
        clearRange.baseArrayLayer = 0;
        clearRange.layerCount = 1;
        vkCmdClearColorImage(commandBuffer, destImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);
    }

    VkRenderPassBeginInfo renderPassBeginInfo = {0};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = vkrt->runtime.renderPass;
    renderPassBeginInfo.framebuffer = vkrt->runtime.framebuffers[imageIndex];
    renderPassBeginInfo.renderArea.offset = (VkOffset2D){0, 0};
    renderPassBeginInfo.renderArea.extent = extent;
    renderPassBeginInfo.clearValueCount = 0;
    renderPassBeginInfo.pClearValues = NULL;

    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (vkrt->appHooks.drawOverlay) {
        vkrt->appHooks.drawOverlay(vkrt, commandBuffer, vkrt->appHooks.userData);
    }

    vkCmdEndRenderPass(commandBuffer);

    if (vkrt->core.descriptorSetReady) {
        transitionImageLayout(commandBuffer, accumulationReadImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        transitionImageLayout(commandBuffer, accumulationWriteImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vkrt->runtime.timestampPool, qbase + 1);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        perror("[ERROR]: Failed to end command buffer");
        exit(EXIT_FAILURE);
    }
}

void drawFrame(VKRT* vkrt) {
    if (!vkrt) return;
    VKRT_beginFrame(vkrt);
    VKRT_updateScene(vkrt);
    VKRT_trace(vkrt);
    VKRT_present(vkrt);
    VKRT_endFrame(vkrt);
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
        printf("No case handling transition from %d to %d\n", oldLayout, newLayout);
        perror("[ERROR]: Unsupported layout transition");
        exit(EXIT_FAILURE);
    }

    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

void createStorageImage(VKRT* vkrt) {
    VkImageCreateInfo imageCreateInfo = {0};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageCreateInfo.extent.width = vkrt->runtime.swapChainExtent.width;
    imageCreateInfo.extent.height = vkrt->runtime.swapChainExtent.height;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    for (uint32_t i = 0; i < 2; i++) {
        if (vkCreateImage(vkrt->core.device, &imageCreateInfo, NULL, &vkrt->core.accumulationImages[i]) != VK_SUCCESS) {
            perror("[ERROR]: Failed to create accumulation image");
            exit(EXIT_FAILURE);
        }

        VkMemoryRequirements memoryRequirements;
        vkGetImageMemoryRequirements(vkrt->core.device, vkrt->core.accumulationImages[i], &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo = {0};
        memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.allocationSize = memoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex = findMemoryType(vkrt, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(vkrt->core.device, &memoryAllocateInfo, NULL, &vkrt->core.accumulationImageMemories[i]) != VK_SUCCESS) {
            perror("[ERROR]: Failed to allocate accumulation image memory");
            exit(EXIT_FAILURE);
        }

        if (vkBindImageMemory(vkrt->core.device, vkrt->core.accumulationImages[i], vkrt->core.accumulationImageMemories[i], 0) != VK_SUCCESS) {
            perror("[ERROR]: Failed to bind accumulation image memory");
            exit(EXIT_FAILURE);
        }

        VkImageViewCreateInfo imageViewCreateInfo = {0};
        imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewCreateInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        imageViewCreateInfo.subresourceRange.levelCount = 1;
        imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        imageViewCreateInfo.subresourceRange.layerCount = 1;
        imageViewCreateInfo.image = vkrt->core.accumulationImages[i];

        if (vkCreateImageView(vkrt->core.device, &imageViewCreateInfo, NULL, &vkrt->core.accumulationImageViews[i]) != VK_SUCCESS) {
            perror("[ERROR]: Failed to create accumulation image view");
            exit(EXIT_FAILURE);
        }
    }

    vkrt->core.accumulationReadIndex = 0;
    vkrt->core.accumulationWriteIndex = 1;
    vkrt->core.storageImage = vkrt->core.accumulationImages[1];
    vkrt->core.storageImageView = vkrt->core.accumulationImageViews[1];
    vkrt->core.storageImageMemory = vkrt->core.accumulationImageMemories[1];

    VkCommandBuffer commandBuffer = beginSingleTimeCommands(vkrt);
    transitionImageLayout(commandBuffer, vkrt->core.accumulationImages[0], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    transitionImageLayout(commandBuffer, vkrt->core.accumulationImages[1], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    endSingleTimeCommands(vkrt, commandBuffer);
}
