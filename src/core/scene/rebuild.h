#pragma once

#include "vkrt_internal.h"

#include <stdint.h>

void vkrtCleanupPendingGeometryUploads(VKRT* vkrt, FrameSceneUpdate* update);
void vkrtCleanupPendingBLASBuilds(VKRT* vkrt, FrameSceneUpdate* update);
void vkrtCleanupFrameSceneUpdate(VKRT* vkrt, uint32_t frameIndex);
void vkrtDestroyMeshAccelerationStructure(VKRT* vkrt, Mesh* mesh);
VKRT_Result vkrtSceneRebuildMaterialBuffer(VKRT* vkrt);
VKRT_Result vkrtSceneRebuildTopLevelAccelerationStructures(VKRT* vkrt);
