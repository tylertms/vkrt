#include "geometry.h"

#include "constants.h"
#include "state.h"
#include "types.h"
#include "vkrt_types.h"

#include <stddef.h>
#include <stdint.h>

static int meshUploadValid(const VKRT_MeshUpload* upload) {
    return upload && upload->vertices && upload->indices && upload->vertexCount > 0u && upload->indexCount > 0u &&
           upload->vertexCount <= (size_t)VKRT_INVALID_INDEX && upload->indexCount <= (size_t)VKRT_INVALID_INDEX;
}

VKRT_Result VKRT_uploadMeshData(
    VKRT* vkrt,
    const Vertex* vertices,
    size_t vertexCount,
    const uint32_t* indices,
    size_t indexCount
) {
    if (!vkrt || !vertices || !indices || vertexCount == 0u || indexCount == 0u) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    return vkrtSceneUploadMeshData(vkrt, vertices, vertexCount, indices, indexCount);
}

VKRT_Result VKRT_uploadMeshDataBatch(VKRT* vkrt, const VKRT_MeshUpload* uploads, size_t uploadCount) {
    if (!vkrt || !uploads || uploadCount == 0u || uploadCount > (size_t)VKRT_INVALID_INDEX) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < uploadCount; i++) {
        if (!meshUploadValid(&uploads[i])) return VKRT_ERROR_INVALID_ARGUMENT;
    }
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    return vkrtSceneUploadMeshDataBatch(vkrt, uploads, uploadCount);
}

VKRT_Result VKRT_removeMesh(VKRT* vkrt, uint32_t meshIndex) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (meshIndex >= vkrt->core.meshCount) return VKRT_ERROR_INVALID_ARGUMENT;
    return vkrtSceneRemoveMesh(vkrt, meshIndex);
}
