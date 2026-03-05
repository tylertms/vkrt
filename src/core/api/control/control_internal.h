#pragma once

#include "vkrt_internal.h"

void destroyMeshAccelerationStructure(VKRT* vkrt, Mesh* mesh);
VKRT_Result rebuildSceneGeometry(VKRT* vkrt);
VKRT_Result rebuildLightBuffers(VKRT* vkrt);
VKRT_Result rebuildMaterialBuffer(VKRT* vkrt);
VKRT_Result rebuildTopLevelScene(VKRT* vkrt);
