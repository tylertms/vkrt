#pragma once
#include "vkrt.h"

void createBuffer(VKRT* vkrt, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* bufferMemory);
void copyBuffer(VKRT* vkrt, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
VkDeviceAddress createBufferFromHostData(VKRT* vkrt, const void* hostData, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* outBuffer, VkDeviceMemory* outMemory);