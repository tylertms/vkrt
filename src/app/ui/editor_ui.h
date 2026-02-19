#pragma once

#include "vkrt.h"

void editorUIInitialize(VKRT* runtime, void* userData);
void editorUIShutdown(VKRT* runtime, void* userData);
void editorUIDraw(VKRT* runtime, VkCommandBuffer commandBuffer, void* userData);
