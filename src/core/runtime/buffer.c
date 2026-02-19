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

    if (vkCreateBuffer(vkrt->core.device, &bufferCreateInfo, NULL, buffer) != VK_SUCCESS) {
        perror("[ERROR]: Failed to create buffer");
        exit(EXIT_FAILURE);
    }

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(vkrt->core.device, *buffer, &memoryRequirements);

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

    if (vkAllocateMemory(vkrt->core.device, &memoryAllocateInfo, NULL, bufferMemory) != VK_SUCCESS) {
        perror("[ERROR]: Failed to allocate buffer memory");
        exit(EXIT_FAILURE);
    }

    vkBindBufferMemory(vkrt->core.device, *buffer, *bufferMemory, 0);
}

void copyBuffer(VKRT* vkrt, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
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

    VkBufferCopy copyRegion = {0};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(vkrt->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vkrt->core.graphicsQueue);
    vkFreeCommandBuffers(vkrt->core.device, vkrt->runtime.commandPool, 1, &commandBuffer);
}

VkDeviceAddress createBufferFromHostData(VKRT* vkrt, const void* hostData, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* outBuffer, VkDeviceMemory* outMemory) {
    VkBuffer stagingBuf;
    VkDeviceMemory stagingMem;
    createBuffer(vkrt, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuf, &stagingMem);

    void* mapped;
    vkMapMemory(vkrt->core.device, stagingMem, 0, size, 0, &mapped);
    memcpy(mapped, hostData, (size_t)size);
    vkUnmapMemory(vkrt->core.device, stagingMem);

    createBuffer(vkrt, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outBuffer, outMemory);

    copyBuffer(vkrt, stagingBuf, *outBuffer, size);

    vkDestroyBuffer(vkrt->core.device, stagingBuf, NULL);
    vkFreeMemory(vkrt->core.device, stagingMem, NULL);

    VkBufferDeviceAddressInfo addrInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = *outBuffer
    };

    PFN_vkGetBufferDeviceAddressKHR pvkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(vkrt->core.device, "vkGetBufferDeviceAddressKHR");
    return pvkGetBufferDeviceAddressKHR(vkrt->core.device, &addrInfo);
}

VkDeviceAddress appendBufferFromHostData(VKRT* vkrt, const void* hostData, VkDeviceSize appendSize, VkBufferUsageFlags usage, VkBuffer* ioBuffer, VkDeviceMemory* ioMemory, VkDeviceSize oldSize) {
    if (*ioBuffer == VK_NULL_HANDLE || oldSize == 0) {
        return createBufferFromHostData(vkrt, hostData, appendSize, usage, ioBuffer, ioMemory);
    }

    VkDeviceSize newSize = oldSize + appendSize;

    VkBuffer stagingNewBuf;
    VkDeviceMemory stagingNewMem;
    createBuffer(vkrt, appendSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingNewBuf, &stagingNewMem);

    void* mapped = NULL;
    vkMapMemory(vkrt->core.device, stagingNewMem, 0, appendSize, 0, &mapped);
    memcpy(mapped, hostData, (size_t)appendSize);
    vkUnmapMemory(vkrt->core.device, stagingNewMem);

    VkBuffer newBuffer = VK_NULL_HANDLE;
    VkDeviceMemory newMemory = VK_NULL_HANDLE;
    VkBufferUsageFlags newUsage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    createBuffer(vkrt, newSize, newUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &newBuffer, &newMemory);

    VkCommandBufferAllocateInfo commandBufferAllocateInfo = {0};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandPool = vkrt->runtime.commandPool;
    commandBufferAllocateInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(vkrt->core.device, &commandBufferAllocateInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy regions[2];
    memset(regions, 0, sizeof(regions));
    regions[0].srcOffset = 0;
    regions[0].dstOffset = 0;
    regions[0].size = oldSize;
    vkCmdCopyBuffer(cmd, *ioBuffer, newBuffer, 1, &regions[0]);

    regions[1].srcOffset = 0;
    regions[1].dstOffset = oldSize;
    regions[1].size = appendSize;
    vkCmdCopyBuffer(cmd, stagingNewBuf, newBuffer, 1, &regions[1]);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(vkrt->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vkrt->core.graphicsQueue);

    vkFreeCommandBuffers(vkrt->core.device, vkrt->runtime.commandPool, 1, &cmd);

    vkDestroyBuffer(vkrt->core.device, stagingNewBuf, NULL);
    vkFreeMemory(vkrt->core.device, stagingNewMem, NULL);

    vkDestroyBuffer(vkrt->core.device, *ioBuffer, NULL);
    vkFreeMemory(vkrt->core.device, *ioMemory, NULL);

    *ioBuffer = newBuffer;
    *ioMemory = newMemory;

    VkBufferDeviceAddressInfo addrInfo = {0};
    addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.buffer = *ioBuffer;
    PFN_vkGetBufferDeviceAddressKHR pvkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(vkrt->core.device, "vkGetBufferDeviceAddressKHR");

    return pvkGetBufferDeviceAddressKHR(vkrt->core.device, &addrInfo);
}
