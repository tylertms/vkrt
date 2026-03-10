#pragma once

#include "vkrt_internal.h"

#include <stdint.h>

VKRT_Result vkrtRequireSceneStateReady(const VKRT* vkrt);
VKRT_Result vkrtWaitForAllInFlightFrames(const VKRT* vkrt);
uint32_t vkrtResolveMeshRenderBackfaces(const Mesh* mesh);
void vkrtDestroyAccelerationStructureResources(VKRT* vkrt, AccelerationStructure* accelerationStructure);
void vkrtMarkSceneResourcesDirty(VKRT* vkrt);
void vkrtMarkMaterialResourcesDirty(VKRT* vkrt);
void vkrtMarkLightResourcesDirty(VKRT* vkrt);
