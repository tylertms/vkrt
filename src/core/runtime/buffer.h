#pragma once

#include "vkrt_internal.h"

VKRT_Result createBuffer(
    VKRT* vkrt,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer* buffer,
    VkDeviceMemory* bufferMemory
);
VKRT_Result copyBuffer(VKRT* vkrt, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
VKRT_Result createHostBufferFromData(
    VKRT* vkrt,
    const void* hostData,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer* outBuffer,
    VkDeviceMemory* outMemory,
    VkDeviceAddress* outDeviceAddress
);
VKRT_Result createDeviceBufferFromData(
    VKRT* vkrt,
    const void* hostData,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer* outBuffer,
    VkDeviceMemory* outMemory,
    VkDeviceAddress* outDeviceAddress
);
VKRT_Result createDeviceBufferFromDataImmediate(
    VKRT* vkrt,
    const void* hostData,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer* outBuffer,
    VkDeviceMemory* outMemory,
    VkDeviceAddress* outDeviceAddress
);
VKRT_Result createZeroInitializedDeviceBuffer(
    VKRT* vkrt,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    Buffer* outBuffer
);
VkDeviceAddress queryBufferDeviceAddress(VKRT* vkrt, VkBuffer buffer);
void destroyBufferResources(VKRT* vkrt, Buffer* buffer);
void destroyTransfer(VKRT* vkrt, FrameTransfer* transfer);
