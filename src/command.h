#pragma once
#include "vkrt.h"

void createCommandPool(VKRT* vkrt);
void createCommandBuffers(VKRT* vkrt);
void recordCommandBuffer(VKRT* vkrt, uint32_t imageIndex);
void drawFrame(VKRT* vkrt);
VkCommandBuffer beginSingleTimeCommands(VKRT* vkrt);
void endSingleTimeCommands(VKRT* vkrt, VkCommandBuffer commandBuffer);
void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
void createStorageImage(VKRT* vkrt);