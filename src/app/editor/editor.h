#pragma once

#include "vkrt.h"
#include "session.h"

void editorUIInitialize(VKRT* vkrt, void* userData);
void editorUIShutdown(VKRT* vkrt, void* userData);
void editorUIUpdate(VKRT* vkrt, Session* session);
void editorUIDraw(VKRT* vkrt, VkCommandBuffer commandBuffer, void* userData);
void editorUIInitializeDialogs(GLFWwindow* window);
void editorUIShutdownDialogs(void);
void editorUIProcessDialogs(Session* session);
