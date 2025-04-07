#include "command.h"
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

    VkRenderPassBeginInfo renderPassInfo = {0};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = vkrt->renderPass;
    renderPassInfo.framebuffer = vkrt->swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = (VkOffset2D){0, 0};
    renderPassInfo.renderArea.extent = vkrt->swapChainExtent;

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass2(vkrt->commandBuffers[vkrt->currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(vkrt->commandBuffers[vkrt->currentFrame], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, vkrt->rayTracingPipeline);

    VkViewport viewport = {0};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)vkrt->swapChainExtent.width;
    viewport.height = (float)vkrt->swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(vkrt->commandBuffers[vkrt->currentFrame], 0, 1, &viewport);

    VkRect2D scissor = {0};
    scissor.offset = (VkOffset2D){0, 0};
    scissor.extent = vkrt->swapChainExtent;
    vkCmdSetScissor(vkrt->commandBuffers[vkrt->currentFrame], 0, 1, &scissor);

    // TODO
    //vkCmdTraceRaysKHR(vkrt->commandBuffer, &vkrt->shaderBindingTables[0], &vkrt->shaderBindingTables[1], &vkrt->shaderBindingTables[2], &vkrt->shaderBindingTables[3], vkrt->swapChainExtent.width, vkrt->swapChainExtent.height, 1);

    vkCmdEndRenderPass(vkrt->commandBuffers[vkrt->currentFrame]);

    if (vkEndCommandBuffer(vkrt->commandBuffers[vkrt->currentFrame]) != VK_SUCCESS) {
        perror("ERROR: Failed to record the command buffer");
        exit(EXIT_FAILURE);
    }
}

void drawFrame(VKRT* vkrt) {
    vkWaitForFences(vkrt->device, 1, &vkrt->inFlightFences[vkrt->currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(vkrt->device, 1, &vkrt->inFlightFences[vkrt->currentFrame]);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(vkrt->device, vkrt->swapChain, UINT64_MAX, vkrt->imageAvailableSemaphores[vkrt->currentFrame], VK_NULL_HANDLE, &imageIndex);

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

    vkQueuePresentKHR(vkrt->presentQueue, &presentInfo);

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