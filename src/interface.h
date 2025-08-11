#pragma once
#include "vkrt.h"

void initImGui(VKRT* vkrt);
void deinitImGui(VKRT* vkrt);
void drawInterface(VKRT* vkrt);
void pollCameraMovement(VKRT* vkrt);
void setupSceneUniform(VKRT* vkrt);
void resetSceneFrame(VKRT* vkrt);
void updateMatricesFromCamera(VKRT* vkrt);
void setDarkTheme();