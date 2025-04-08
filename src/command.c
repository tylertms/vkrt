#include "command.h"
#include "buffer.h"
#include "device.h"
#include "swapchain.h"

#include <stdlib.h>
#include <stdio.h>

void createCommandPool(VKRT* vkrt) {
    QueueFamily indices = findQueueFamilies(vkrt);

    VkCommandPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = indices.graphics;

    if (vkCreateCommandPool(vkrt->device, &poolInfo, NULL, &vkrt->commandPool) != VK_SUCCESS) {
        perror("ERROR: Failed to create command pool");
        exit(EXIT_FAILURE);
    }
}

void createCommandBuffers(VKRT* vkrt) {
    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = vkrt->commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    
    if (vkAllocateCommandBuffers(vkrt->device, &allocInfo, vkrt->commandBuffers) != VK_SUCCESS) {
        perror("ERROR: Failed to allocate command buffers");
        exit(EXIT_FAILURE);
    }
}

void recordCommandBuffer(VKRT* vkrt, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = NULL;

    if (vkBeginCommandBuffer(vkrt->commandBuffers[vkrt->currentFrame], &beginInfo) != VK_SUCCESS) {
        perror("ERROR: Failed to begin recording command buffer");
        exit(EXIT_FAILURE);
    }

    vkCmdBindPipeline(vkrt->commandBuffers[vkrt->currentFrame], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vkrt->rayTracingPipeline);
    vkCmdBindDescriptorSets(vkrt->commandBuffers[vkrt->currentFrame], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vkrt->pipelineLayout, 0, 1, &vkrt->descriptorSet, 0, NULL);

    PFN_vkCmdTraceRaysKHR pvkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(vkrt->device, "vkCmdTraceRaysKHR");
    pvkCmdTraceRaysKHR(vkrt->commandBuffers[vkrt->currentFrame], &vkrt->shaderBindingTables[0], &vkrt->shaderBindingTables[1], &vkrt->shaderBindingTables[2], &vkrt->shaderBindingTables[3], vkrt->swapChainExtent.width, vkrt->swapChainExtent.height, 1);

    transitionImageLayout(vkrt, vkrt->swapChainImages[imageIndex], vkrt->swapChainImageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    transitionImageLayout(vkrt, vkrt->storageImage, vkrt->swapChainImageFormat, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkImageCopy copyRegion = {0};
    copyRegion.srcSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.srcOffset = (VkOffset3D){ 0, 0, 0 };
    copyRegion.dstSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.dstOffset = (VkOffset3D){ 0, 0, 0 };
    copyRegion.extent = (VkExtent3D){ vkrt->swapChainExtent.width, vkrt->swapChainExtent.height, 1 };

    vkCmdCopyImage(vkrt->commandBuffers[vkrt->currentFrame], vkrt->storageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vkrt->swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    transitionImageLayout(vkrt, vkrt->swapChainImages[imageIndex], vkrt->swapChainImageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    transitionImageLayout(vkrt, vkrt->storageImage, vkrt->swapChainImageFormat, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

    if (vkEndCommandBuffer(vkrt->commandBuffers[vkrt->currentFrame]) != VK_SUCCESS) {
        perror("ERROR: Failed to record the command buffer");
        exit(EXIT_FAILURE);
    }
}

void drawFrame(VKRT* vkrt) {
    vkWaitForFences(vkrt->device, 1, &vkrt->inFlightFences[vkrt->currentFrame], VK_TRUE, UINT64_MAX);
    
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(vkrt->device, vkrt->swapChain, UINT64_MAX, vkrt->imageAvailableSemaphores[vkrt->currentFrame], VK_NULL_HANDLE, &imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain(vkrt);
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        perror("ERROR: Failed to acquire swap chain image");
        exit(EXIT_FAILURE);
    }
    
    vkResetFences(vkrt->device, 1, &vkrt->inFlightFences[vkrt->currentFrame]);

    vkResetCommandBuffer(vkrt->commandBuffers[vkrt->currentFrame], 0);
    recordCommandBuffer(vkrt, imageIndex);

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {vkrt->imageAvailableSemaphores[vkrt->currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vkrt->commandBuffers[vkrt->currentFrame];

    VkSemaphore signalSemaphores[] = {vkrt->renderFinishedSemaphores[vkrt->currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(vkrt->graphicsQueue, 1, &submitInfo, vkrt->inFlightFences[vkrt->currentFrame]) != VK_SUCCESS) {
        perror("ERROR: Failed to submit draw command buffer");
        exit(EXIT_FAILURE);
    }

    VkPresentInfoKHR presentInfo = {0};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {vkrt->swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;

    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(vkrt->presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || vkrt->framebufferResized) {
        vkrt->framebufferResized = VK_FALSE;
        recreateSwapChain(vkrt);
    } else if (result != VK_SUCCESS) {
        perror("ERROR: Failed to present swap chain image");
        exit(EXIT_FAILURE);
    }

    vkrt->currentFrame = (vkrt->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void setupShaderBindingTable(VKRT* vkrt) {
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties;
    rayTracingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

    vkGetPhysicalDeviceProperties(vkrt->physicalDevice, (void*)&rayTracingProperties);

    VkDeviceSize bindingTableSize = rayTracingProperties.shaderGroupBaseAlignment * 4;

    QueueFamily indices = findQueueFamilies(vkrt);

    VkBufferCreateInfo bindingTableCreateInfo;
    bindingTableCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bindingTableCreateInfo.size = bindingTableSize;
    bindingTableCreateInfo.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    bindingTableCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bindingTableCreateInfo.queueFamilyIndexCount = 1;
    bindingTableCreateInfo.pQueueFamilyIndices = (uint32_t*)&indices.graphics;

    if (vkCreateBuffer(vkrt->device, &bindingTableCreateInfo, NULL, &vkrt->shaderBindingTableBuffer) != VK_SUCCESS) {
        perror("ERROR: Failed to create shader binding table buffer");
        exit(EXIT_FAILURE);
    }
}

VkCommandBuffer beginSingleTimeCommands(VKRT* vkrt) {
    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = vkrt->commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(vkrt->device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void endSingleTimeCommands(VKRT* vkrt, VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(vkrt->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vkrt->graphicsQueue);

    vkFreeCommandBuffers(vkrt->device, vkrt->commandPool, 1, &commandBuffer);
}


void transitionImageLayout(VKRT* vkrt, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands(vkrt);

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

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        perror("ERROR: Unsupported layout transition");
        exit(EXIT_FAILURE);
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );

    endSingleTimeCommands(vkrt, commandBuffer);
}


void createStorageImage(VKRT* vkrt) {
    VkImageCreateInfo image = {0};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = vkrt->swapChainImageFormat;
    image.extent.width = vkrt->swapChainExtent.width;
    image.extent.height = vkrt->swapChainExtent.height;
    image.extent.depth = 1;
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(vkrt->device, &image, NULL, &vkrt->storageImage) != VK_SUCCESS) {
        perror("ERROR: Failed to create storage image");
        exit(EXIT_FAILURE);
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(vkrt->device, vkrt->storageImage, &memReqs);

    VkMemoryAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(vkrt, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(vkrt->device, &allocInfo, NULL, &vkrt->storageImageMemory) != VK_SUCCESS) {
        perror("ERROR: Failed to allocate storage image memory");
        exit(EXIT_FAILURE);
    }

    if (vkBindImageMemory(vkrt->device, vkrt->storageImage, vkrt->storageImageMemory, 0) != VK_SUCCESS) {
        perror("ERROR: Failed to bind storage image memory");
        exit(EXIT_FAILURE);
    }

    VkImageViewCreateInfo colorImageView = {0};
    colorImageView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorImageView.format = vkrt->swapChainImageFormat;
    colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorImageView.subresourceRange.baseMipLevel = 0;
    colorImageView.subresourceRange.levelCount = 1;
    colorImageView.subresourceRange.baseArrayLayer = 0;
    colorImageView.subresourceRange.layerCount = 1;
    colorImageView.image = vkrt->storageImage;

    if (vkCreateImageView(vkrt->device, &colorImageView, NULL, &vkrt->storageImageView) != VK_SUCCESS) {
        perror("ERROR: Failed to create storage image view");
        exit(EXIT_FAILURE);
    }

    VkCommandBuffer cmdBuffer = beginSingleTimeCommands(vkrt);
    transitionImageLayout(vkrt, vkrt->storageImage, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    endSingleTimeCommands(vkrt, cmdBuffer);
}