#pragma once

#include "vkrt_internal.h"

void applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input);
void recordFrameTime(VKRT* vkrt, uint32_t frameIndex);
VKRT_Result createSceneUniform(VKRT* vkrt);
void markSelectionMaskDirty(VKRT* vkrt);
void resetSceneData(VKRT* vkrt);
void syncSceneStateData(VKRT* vkrt);
void syncSelectionSceneData(VKRT* vkrt);
void syncCurrentFrameSceneData(VKRT* vkrt);
void syncCameraMatrices(VKRT* vkrt);
void updateCamera(VKRT* vkrt);
void updateAutoSPP(VKRT* vkrt);
void decomposeImportedMeshTransform(mat4 worldTransform, vec3 outPosition, vec3 outRotation, vec3 outScale);
VkTransformMatrixKHR getMeshTransform(MeshInfo* meshInfo);
