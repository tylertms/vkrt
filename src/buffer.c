#include "buffer.h"
#include "device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void createBuffer(VKRT* vkrt, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* bufferMemory) {
    VkBufferCreateInfo bufferCreateInfo = {0};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkrt->device, &bufferCreateInfo, NULL, buffer) != VK_SUCCESS) {
        perror("ERROR: Failed to create buffer");
        exit(EXIT_FAILURE);
    }

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(vkrt->device, *buffer, &memoryRequirements);

    VkMemoryAllocateInfo memoryAllocateInfo = {0};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = findMemoryType(vkrt, memoryRequirements.memoryTypeBits, properties);

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo = {0};
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    }

    if (vkAllocateMemory(vkrt->device, &memoryAllocateInfo, NULL, bufferMemory) != VK_SUCCESS) {
        perror("ERROR: Failed to allocate buffer memory");
        exit(EXIT_FAILURE);
    }

    vkBindBufferMemory(vkrt->device, *buffer, *bufferMemory, 0);
}

void copyBuffer(VKRT* vkrt, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {0};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandPool = vkrt->commandPool;
    commandBufferAllocateInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(vkrt->device, &commandBufferAllocateInfo, &commandBuffer);

    VkCommandBufferBeginInfo commandBufferBeginInfo = {0};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);

    VkBufferCopy copyRegion = {0};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(vkrt->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vkrt->graphicsQueue);
    vkFreeCommandBuffers(vkrt->device, vkrt->commandPool, 1, &commandBuffer);
}

VkDeviceAddress createBufferFromHostData(VKRT* vkrt, const void* hostData, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* outBuffer, VkDeviceMemory* outMemory) {
    VkBuffer stagingBuf;
    VkDeviceMemory stagingMem;
    createBuffer(vkrt, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuf, &stagingMem);

    void* mapped;
    vkMapMemory(vkrt->device, stagingMem, 0, size, 0, &mapped);
    memcpy(mapped, hostData, (size_t)size);
    vkUnmapMemory(vkrt->device, stagingMem);

    createBuffer(vkrt, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outBuffer, outMemory);

    copyBuffer(vkrt, stagingBuf, *outBuffer, size);

    vkDestroyBuffer(vkrt->device, stagingBuf, NULL);
    vkFreeMemory(vkrt->device, stagingMem, NULL);

    VkBufferDeviceAddressInfo addrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = *outBuffer};
    PFN_vkGetBufferDeviceAddressKHR pvkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(vkrt->device, "vkGetBufferDeviceAddressKHR");
    return pvkGetBufferDeviceAddressKHR(vkrt->device, &addrInfo);
}