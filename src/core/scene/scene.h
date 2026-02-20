#pragma once
#include "vkrt.h"

void applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input);
void recordFrameTime(VKRT* vkrt);
void createSceneUniform(VKRT* vkrt);
void resetSceneData(VKRT* vkrt);
void updateMatricesFromCamera(VKRT* vkrt);
void updateAutoSPP(VKRT* vkrt);
VkTransformMatrixKHR getMeshTransform(MeshInfo* meshInfo);
