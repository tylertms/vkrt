#pragma once

#include "vkrt.h"

void editorUIInitialize(VKRT* vkrt, void* userData);
void editorUIShutdown(VKRT* vkrt, void* userData);
void editorUIDraw(VKRT* vkrt, VkCommandBuffer commandBuffer, void* userData);
