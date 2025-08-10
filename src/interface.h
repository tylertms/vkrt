#pragma once
#include "vkrt.h"

void setupImGui(VKRT* vkrt);
void deinitImGui(VKRT* vkrt);
void drawInterface(VKRT* vkrt);
void handleCameraMovement(VKRT* vkrt);
void setupSceneUniform(VKRT* vkrt);
void updateMatricesFromCamera(VKRT* vkrt);
void setDarkTheme();