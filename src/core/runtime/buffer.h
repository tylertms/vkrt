#pragma once

#include "vkrt_internal.h"

VKRT_Result createBuffer(VKRT* vkrt, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* bufferMemory);
VKRT_Result copyBuffer(VKRT* vkrt, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
VKRT_Result createBufferFromHostData(VKRT* vkrt, const void* hostData, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* outBuffer, VkDeviceMemory* outMemory, VkDeviceAddress* outDeviceAddress);
