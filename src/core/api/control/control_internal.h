#pragma once

#include "vkrt_internal.h"

static inline VKRT_Result requireSceneStateReady(const VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->core.sceneData) return VKRT_ERROR_OPERATION_FAILED;
    return VKRT_SUCCESS;
}

void destroyMeshAccelerationStructure(VKRT* vkrt, Mesh* mesh);
VKRT_Result rebuildSceneGeometry(VKRT* vkrt);
VKRT_Result rebuildLightBuffers(VKRT* vkrt);
VKRT_Result rebuildMaterialBuffer(VKRT* vkrt);
VKRT_Result rebuildTopLevelScene(VKRT* vkrt);
