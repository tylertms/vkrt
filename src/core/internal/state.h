#pragma once

#include "vkrt_internal.h"

#include <stdint.h>

VKRT_Result vkrtRequireSceneStateReady(const VKRT* vkrt);
VKRT_Result vkrtWaitForAllInFlightFrames(const VKRT* vkrt);
VKRT_Result vkrtConvertVkResult(VkResult result);
VKRT_Result vkrtEnsureDefaultMaterial(VKRT* vkrt);
const SceneMaterial* vkrtGetSceneMaterial(const VKRT* vkrt, uint32_t materialIndex);
const Material* vkrtGetSceneMaterialData(const VKRT* vkrt, uint32_t materialIndex);
uint32_t vkrtCountMaterialUsers(const VKRT* vkrt, uint32_t materialIndex);
uint32_t vkrtResolveMeshRenderBackfaces(const Mesh* mesh);
VKRT_Result vkrtReleaseTextureIfUnused(VKRT* vkrt, uint32_t textureIndex);
void vkrtDestroyAccelerationStructureResources(VKRT* vkrt, AccelerationStructure* accelerationStructure);
void vkrtMarkSceneResourcesDirty(VKRT* vkrt);
void vkrtMarkSelectionResourcesDirty(VKRT* vkrt);
void vkrtMarkMaterialResourcesDirty(VKRT* vkrt);
void vkrtMarkTextureResourcesDirty(VKRT* vkrt);
void vkrtMarkLightResourcesDirty(VKRT* vkrt);
