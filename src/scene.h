#pragma once
#include "vkrt.h"

void pollCameraMovement(VKRT* vkrt);
void recordFrameTime(VKRT* vkrt);
void createSceneUniform(VKRT* vkrt);
void resetSceneData(VKRT* vkrt);
void updateMatricesFromCamera(VKRT* vkrt);
VkTransformMatrixKHR getMeshTransform(MeshInfo* meshInfo);
void setDefaultStyle();