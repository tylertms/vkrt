#pragma once
#include "vkrt.h"

void createBuffer(VKRT* vkrt, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* bufferMemory);
void copyBuffer(VKRT* vkrt, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
void createUniformBuffer(VKRT* vkrt);
void createVertexBuffer(VKRT* vkrt);
void createIndexBuffer(VKRT* vkrt);