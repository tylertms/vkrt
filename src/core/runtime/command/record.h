#pragma once

#include "vkrt_internal.h"

VKRT_Result recordCommandBuffer(VKRT* vkrt, uint32_t imageIndex, VkBool32 presentToSwapchain);
void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
