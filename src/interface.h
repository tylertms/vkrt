#pragma once
#include "vkrt.h"

void pollCameraMovement(VKRT* vkrt);
void recordFrameTime(VKRT* vkrt);
void setupSceneUniform(VKRT* vkrt);
void resetSceneFrame(VKRT* vkrt);
void updateMatricesFromCamera(VKRT* vkrt);
void setDefaultStyle();