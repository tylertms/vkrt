#pragma once
#include "vkrt.h"

void createCommandPool(VKRT* vkrt);
void createCommandBuffers(VKRT* vkrt);
void recordCommandBuffer(VKRT* vkrt, uint32_t imageIndex);
void drawFrame(VKRT* vkrt);
void setupShaderBindingTable(VKRT* vkrt);
VkCommandBuffer beginSingleTimeCommands(VKRT* vkrt);
void endSingleTimeCommands(VKRT* vkrt, VkCommandBuffer commandBuffer);
void transitionImageLayout(VKRT* vkrt, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
void createStorageImage(VKRT* vkrt);