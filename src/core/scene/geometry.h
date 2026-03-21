#pragma once

#include "vkrt_internal.h"

#include <stddef.h>
#include <stdint.h>

VKRT_Result vkrtScenePreparePendingGeometryUploads(VKRT* vkrt);
VKRT_Result vkrtSceneRebuildMeshInfoBuffer(VKRT* vkrt);
VKRT_Result vkrtSceneBuildMeshInfoBuffer(VKRT* vkrt, Buffer* outBuffer);
VKRT_Result vkrtSceneUploadMeshData(
    VKRT* vkrt,
    const Vertex* vertices,
    size_t vertexCount,
    const uint32_t* indices,
    size_t indexCount
);
VKRT_Result vkrtSceneUploadMeshDataBatch(VKRT* vkrt, const VKRT_MeshUpload* uploads, size_t uploadCount);
VKRT_Result vkrtSceneRemoveMesh(VKRT* vkrt, uint32_t meshIndex);
