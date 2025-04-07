#pragma once
#include "vkrt.h"

void createCommandPool(VKRT* vkrt);
void createCommandBuffers(VKRT* vkrt);
void recordCommandBuffer(VKRT* vkrt, uint32_t imageIndex);
void drawFrame(VKRT* vkrt);
void setupShaderBindingTable(VKRT* vkrt);