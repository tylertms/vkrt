#pragma once

#include "vkrt_internal.h"

#include <stdint.h>

VKRT_Result vkrtRequireSceneStateReady(const VKRT* vkrt);
VKRT_Result vkrtWaitForAllInFlightFrames(const VKRT* vkrt);
uint32_t vkrtResolveMeshRenderBackfaces(const Mesh* mesh);
void vkrtMarkSceneResourcesDirty(VKRT* vkrt);
void vkrtMarkMaterialResourcesDirty(VKRT* vkrt);
void vkrtMarkLightResourcesDirty(VKRT* vkrt);
void destroyMeshAccelerationStructure(VKRT* vkrt, Mesh* mesh);
VKRT_Result preparePendingGeometryUploads(VKRT* vkrt);
VKRT_Result rebuildLightBuffers(VKRT* vkrt);
VKRT_Result rebuildMaterialBuffer(VKRT* vkrt);
VKRT_Result rebuildTopLevelScene(VKRT* vkrt);
void cleanupPendingGeometryUploads(VKRT* vkrt, FrameSceneUpdate* update);
void cleanupPendingBLASBuilds(VKRT* vkrt, FrameSceneUpdate* update);
void cleanupFrameSceneUpdate(VKRT* vkrt, uint32_t frameIndex);
