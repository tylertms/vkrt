#pragma once

#include "vkrt.h"
#include "demo_state.h"

void initGUI(VKRT* vkrt, void* userData);
void deinitGUI(VKRT* vkrt, void* userData);
void drawGUI(VKRT* vkrt, VkCommandBuffer commandBuffer, void* userData);
void setDefaultStyle();
