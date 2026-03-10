#include "geometry.h"

VKRT_Result VKRT_uploadMeshData(
    VKRT* vkrt,
    const Vertex* vertices,
    size_t vertexCount,
    const uint32_t* indices,
    size_t indexCount
) {
    return vkrtSceneUploadMeshData(vkrt, vertices, vertexCount, indices, indexCount);
}

VKRT_Result VKRT_removeMesh(VKRT* vkrt, uint32_t meshIndex) {
    return vkrtSceneRemoveMesh(vkrt, meshIndex);
}
