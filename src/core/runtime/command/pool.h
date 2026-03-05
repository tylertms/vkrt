#pragma once

#include "vkrt_internal.h"

VKRT_Result createCommandPool(VKRT* vkrt);
VKRT_Result createCommandBuffers(VKRT* vkrt);
VKRT_Result beginSingleTimeCommands(VKRT* vkrt, VkCommandBuffer* outCommandBuffer);
VKRT_Result endSingleTimeCommands(VKRT* vkrt, VkCommandBuffer commandBuffer);
