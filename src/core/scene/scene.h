#pragma once

#include "vkrt_internal.h"

void applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input);
void recordFrameTime(VKRT* vkrt, uint32_t frameIndex);
VKRT_Result createSceneUniform(VKRT* vkrt);
VKRT_Result createRGB2SpecResources(VKRT* vkrt);
void markSelectionMaskDirty(VKRT* vkrt);
void resetSceneData(VKRT* vkrt);
void syncSceneStateData(VKRT* vkrt);
void syncAllSceneDataFrames(VKRT* vkrt);
void syncSelectionSceneData(VKRT* vkrt);
void syncCurrentFrameSceneData(VKRT* vkrt);
void syncCameraMatrices(VKRT* vkrt);
void updateCamera(VKRT* vkrt);
void updateAutoSPP(VKRT* vkrt);
void resetAutoSPPState(VKRT* vkrt, VkBool32 resetSamplesPerPixel);
VKRT_Result createAutoExposureReadbacks(VKRT* vkrt);
void destroyAutoExposureReadbacks(VKRT* vkrt);
void resolveAutoExposureReadback(VKRT* vkrt, uint32_t frameIndex);
void recordAutoExposureReadback(
    VKRT* vkrt,
    VkCommandBuffer commandBuffer,
    VkImage accumulationImage,
    VkExtent2D renderExtent
);
void VKRT_buildMeshTransformMatrix(const vec3 position, const vec3 rotationDegrees, const vec3 scale, mat4 outMatrix);
void VKRT_buildImportedNodeTransform(mat4 worldTransform, mat4 outEngineTransform);
void VKRT_decomposeMeshNodeTransform(mat4 worldTransform, vec3 outPosition, vec3 outRotation, vec3 outScale);
void VKRT_decomposeMeshTransform(mat4 worldTransform, vec3 outPosition, vec3 outRotation, vec3 outScale);
VkTransformMatrixKHR getMeshWorldTransform(const Mesh* mesh);
