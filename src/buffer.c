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

void createUniformBuffer(VKRT* vkrt) {
    VkDeviceSize uniformBufferSize = sizeof(SceneUniform);
    createBuffer(vkrt, uniformBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vkrt->uniformBuffer, &vkrt->uniformBufferMemory);
    vkMapMemory(vkrt->device, vkrt->uniformBufferMemory, 0, uniformBufferSize, 0, &vkrt->uniformBufferMapped);
}

void createVertexBuffer(VKRT* vkrt) {
    float vertices[3][3] = {
        {-0.5f, -0.5f, -1.0f},
        {0.5f, -0.5f, -1.0f},
        {0.0f, 0.5f, -1.0f}};

    vkrt->vertexCount = COUNT_OF(vertices);
    VkDeviceSize size = sizeof(vertices);

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createBuffer(vkrt, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingMemory);

    void* data;
    vkMapMemory(vkrt->device, stagingMemory, 0, size, 0, &data);
    memcpy(data, vertices, (size_t)size);
    vkUnmapMemory(vkrt->device, stagingMemory);

    createBuffer(vkrt, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vkrt->vertexBuffer, &vkrt->vertexBufferMemory);

    copyBuffer(vkrt, stagingBuffer, vkrt->vertexBuffer, size);
    vkDestroyBuffer(vkrt->device, stagingBuffer, NULL);
    vkFreeMemory(vkrt->device, stagingMemory, NULL);

    VkBufferDeviceAddressInfo bufferDeviceAddressInfo = {0};
    bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferDeviceAddressInfo.buffer = vkrt->vertexBuffer;

    PFN_vkGetBufferDeviceAddressKHR pvkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(vkrt->device, "vkGetBufferDeviceAddressKHR");
    vkrt->vertexBufferDeviceAddress = pvkGetBufferDeviceAddressKHR(vkrt->device, &bufferDeviceAddressInfo);
}

void createIndexBuffer(VKRT* vkrt) {
    uint32_t indices[3] = {0, 1, 2};

    vkrt->indexCount = COUNT_OF(indices);
    VkDeviceSize size = sizeof(indices);

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createBuffer(vkrt, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingMemory);

    void* data;
    vkMapMemory(vkrt->device, stagingMemory, 0, size, 0, &data);
    memcpy(data, indices, (size_t)size);
    vkUnmapMemory(vkrt->device, stagingMemory);

    createBuffer(vkrt, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &vkrt->indexBuffer, &vkrt->indexBufferMemory);

    copyBuffer(vkrt, stagingBuffer, vkrt->indexBuffer, size);
    vkDestroyBuffer(vkrt->device, stagingBuffer, NULL);
    vkFreeMemory(vkrt->device, stagingMemory, NULL);

    VkBufferDeviceAddressInfo bufferDeviceAddressInfo = {0};
    bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferDeviceAddressInfo.buffer = vkrt->indexBuffer;

    PFN_vkGetBufferDeviceAddressKHR pvkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(vkrt->device, "vkGetBufferDeviceAddressKHR");
    vkrt->indexBufferDeviceAddress = pvkGetBufferDeviceAddressKHR(vkrt->device, &bufferDeviceAddressInfo);
}