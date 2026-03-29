#include "lighting.h"

#include "../../../external/cglm/include/types.h"
#include "buffer.h"
#include "color.h"
#include "constants.h"
#include "debug.h"
#include "scene.h"
#include "state.h"
#include "types.h"
#include "vkrt_engine_types.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vec3.h>

typedef struct LightBufferState {
    Buffer sceneEmissiveMeshData;
    Buffer sceneEmissiveTriangleData;
    Buffer sceneMeshAliasQ;
    Buffer sceneMeshAliasIdx;
    Buffer sceneTriAliasQ;
    Buffer sceneTriAliasIdx;
    uint32_t emissiveMeshCount;
    uint32_t emissiveTriangleCount;
} LightBufferState;

typedef struct EmissiveLightCounts {
    uint32_t emissiveMeshCount;
    uint32_t emissiveTriangleCount;
} EmissiveLightCounts;

typedef struct LightBuildScratch {
    EmissiveMesh* emissiveMeshes;
    EmissiveTriangle* emissiveTriangles;
    float* meshAliasQ;
    uint32_t* meshAliasIdx;
    float* triAliasQ;
    uint32_t* triAliasIdx;
    float* meshWeights;
    float* triPmfScratch;
    uint32_t* sourceMeshIndices;
    uint32_t meshCapacity;
    uint32_t triangleCapacity;
    uint32_t emissiveMeshCount;
    uint32_t emissiveTriangleCount;
    float totalSelectionWeight;
} LightBuildScratch;

static float materialEmissionWeight(const Material* material) {
    if (!isfinite(material->emissionLuminance) || material->emissionLuminance <= 0.0f) return 0.0f;
    float lum = linearSRGBLuminance(material->emissionColor);
    if (lum <= 0.0f) return 0.0f;
    return lum * material->emissionLuminance;
}

static int materialEligibleForDirectLightSampling(const MeshInfo* meshInfo, const Material* material) {
    if (!meshInfo || !material) return 0;
    if (meshInfo->opacity < 0.999f || material->opacity < 0.999f) return 0;
    if (material->emissiveTextureIndex != VKRT_INVALID_INDEX) return 0;
    return material->alphaMode == VKRT_MATERIAL_ALPHA_MODE_OPAQUE;
}

static void transformPosition(const VkTransformMatrixKHR* transform, const vec4 position, vec3 outWorld) {
    outWorld[0] = (transform->matrix[0][0] * position[0]) + (transform->matrix[0][1] * position[1]) +
                  (transform->matrix[0][2] * position[2]) + transform->matrix[0][3];
    outWorld[1] = (transform->matrix[1][0] * position[0]) + (transform->matrix[1][1] * position[1]) +
                  (transform->matrix[1][2] * position[2]) + transform->matrix[1][3];
    outWorld[2] = (transform->matrix[2][0] * position[0]) + (transform->matrix[2][1] * position[1]) +
                  (transform->matrix[2][2] * position[2]) + transform->matrix[2][3];
}

static void destroyLightBufferState(VKRT* vkrt, LightBufferState* state) {
    destroyBufferResources(vkrt, &state->sceneEmissiveMeshData);
    destroyBufferResources(vkrt, &state->sceneEmissiveTriangleData);
    destroyBufferResources(vkrt, &state->sceneMeshAliasQ);
    destroyBufferResources(vkrt, &state->sceneMeshAliasIdx);
    destroyBufferResources(vkrt, &state->sceneTriAliasQ);
    destroyBufferResources(vkrt, &state->sceneTriAliasIdx);
    state->emissiveMeshCount = 0;
    state->emissiveTriangleCount = 0;
}

static void applyLightBufferState(VKRT* vkrt, const LightBufferState* state) {
    vkrt->core.sceneEmissiveMeshData = state->sceneEmissiveMeshData;
    vkrt->core.sceneEmissiveTriangleData = state->sceneEmissiveTriangleData;
    vkrt->core.sceneMeshAliasQ = state->sceneMeshAliasQ;
    vkrt->core.sceneMeshAliasIdx = state->sceneMeshAliasIdx;
    vkrt->core.sceneTriAliasQ = state->sceneTriAliasQ;
    vkrt->core.sceneTriAliasIdx = state->sceneTriAliasIdx;
    vkrt->core.emissiveMeshCount = state->emissiveMeshCount;
    vkrt->core.emissiveTriangleCount = state->emissiveTriangleCount;
    if (vkrt->core.sceneData) {
        vkrt->core.sceneData->emissiveMeshCount = state->emissiveMeshCount;
        vkrt->core.sceneData->emissiveTriangleCount = state->emissiveTriangleCount;
    }
}

static int canAllocateArray(uint32_t count, size_t elementSize) {
    return elementSize == 0 || (size_t)count <= (SIZE_MAX / elementSize);
}

static int buildAliasTable(const float* pmf, uint32_t count, float* outQ, uint32_t* outIdx) {
    if (count == 0) return 1;

    float* scaled = (float*)malloc(sizeof(float) * count);
    uint32_t* smallBin = (uint32_t*)malloc(sizeof(uint32_t) * count);
    uint32_t* largeBin = (uint32_t*)malloc(sizeof(uint32_t) * count);
    if (!scaled || !smallBin || !largeBin) {
        free(scaled);
        free(smallBin);
        free(largeBin);
        return 0;
    }

    uint32_t smallCount = 0;
    uint32_t largeCount = 0;

    for (uint32_t i = 0; i < count; ++i) {
        scaled[i] = pmf[i] * (float)count;
        if (scaled[i] < 1.0f) {
            smallBin[smallCount++] = i;
        } else {
            largeBin[largeCount++] = i;
        }
    }

    while (smallCount > 0 && largeCount > 0) {
        uint32_t smallIndex = smallBin[--smallCount];
        uint32_t largeIndex = largeBin[--largeCount];

        outQ[smallIndex] = scaled[smallIndex];
        outIdx[smallIndex] = largeIndex;

        scaled[largeIndex] = scaled[largeIndex] + scaled[smallIndex] - 1.0f;
        if (scaled[largeIndex] < 1.0f) {
            smallBin[smallCount++] = largeIndex;
        } else {
            largeBin[largeCount++] = largeIndex;
        }
    }

    while (largeCount > 0) {
        uint32_t largeIndex = largeBin[--largeCount];
        outQ[largeIndex] = 1.0f;
        outIdx[largeIndex] = largeIndex;
    }

    while (smallCount > 0) {
        uint32_t smallIndex = smallBin[--smallCount];
        outQ[smallIndex] = 1.0f;
        outIdx[smallIndex] = smallIndex;
    }

    free(scaled);
    free(smallBin);
    free(largeBin);
    return 1;
}

static VKRT_Result uploadLightBuffer(VKRT* vkrt, const void* data, VkDeviceSize size, Buffer* outBuffer) {
    return createDeviceBufferFromData(
        vkrt,
        data,
        size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &outBuffer->buffer,
        &outBuffer->memory,
        &outBuffer->deviceAddress
    );
}

static VKRT_Result countEmissiveLights(const VKRT* vkrt, EmissiveLightCounts* counts) {
    if (!vkrt || !counts) return VKRT_ERROR_INVALID_ARGUMENT;

    *counts = (EmissiveLightCounts){0};
    const uint32_t meshCount = vkrt->core.meshCount;
    uint64_t emissiveMeshCount64 = 0;
    uint64_t emissiveTriangleCount64 = 0;

    for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++) {
        const Mesh* mesh = &vkrt->core.meshes[meshIndex];
        const Material* material = vkrtGetSceneMaterialData(vkrt, mesh->info.materialIndex);
        if (!material || !materialEligibleForDirectLightSampling(&mesh->info, material) ||
            materialEmissionWeight(material) <= 0.0f) {
            continue;
        }
        uint32_t triangleCount = mesh->info.indexCount / 3u;
        if (triangleCount == 0) continue;
        emissiveMeshCount64++;
        emissiveTriangleCount64 += triangleCount;
        if (emissiveMeshCount64 > UINT32_MAX || emissiveTriangleCount64 > UINT32_MAX) {
            LOG_ERROR("Emissive light staging exceeds 32-bit count limits");
            return VKRT_ERROR_OPERATION_FAILED;
        }
    }

    counts->emissiveMeshCount = (uint32_t)emissiveMeshCount64;
    counts->emissiveTriangleCount = (uint32_t)emissiveTriangleCount64;
    return VKRT_SUCCESS;
}

static void clearMeshLightPdfAreas(VKRT* vkrt) {
    if (!vkrt) return;
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        vkrt->core.meshes[i].info.lightPdfArea = 0.0f;
    }
}

static VKRT_Result allocateLightBuildScratch(const EmissiveLightCounts* counts, LightBuildScratch* scratch) {
    if (!counts || !scratch) return VKRT_ERROR_INVALID_ARGUMENT;

    uint32_t allocMeshCount = counts->emissiveMeshCount > 0u ? counts->emissiveMeshCount : 1u;
    uint32_t allocTriangleCount = counts->emissiveTriangleCount > 0u ? counts->emissiveTriangleCount : 1u;
    if (!canAllocateArray(allocMeshCount, sizeof(EmissiveMesh)) ||
        !canAllocateArray(allocTriangleCount, sizeof(EmissiveTriangle)) ||
        !canAllocateArray(allocMeshCount, sizeof(float)) || !canAllocateArray(allocTriangleCount, sizeof(float)) ||
        !canAllocateArray(allocMeshCount, sizeof(uint32_t)) ||
        !canAllocateArray(allocTriangleCount, sizeof(uint32_t))) {
        LOG_ERROR("Emissive light staging allocation exceeds host address space limits");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    *scratch = (LightBuildScratch){
        .emissiveMeshes = (EmissiveMesh*)calloc(allocMeshCount, sizeof(EmissiveMesh)),
        .emissiveTriangles = (EmissiveTriangle*)calloc(allocTriangleCount, sizeof(EmissiveTriangle)),
        .meshAliasQ = (float*)calloc(allocMeshCount, sizeof(float)),
        .meshAliasIdx = (uint32_t*)calloc(allocMeshCount, sizeof(uint32_t)),
        .triAliasQ = (float*)calloc(allocTriangleCount, sizeof(float)),
        .triAliasIdx = (uint32_t*)calloc(allocTriangleCount, sizeof(uint32_t)),
        .meshWeights = (float*)calloc(allocMeshCount, sizeof(float)),
        .triPmfScratch = (float*)calloc(allocTriangleCount, sizeof(float)),
        .sourceMeshIndices = (uint32_t*)calloc(allocMeshCount, sizeof(uint32_t)),
        .meshCapacity = allocMeshCount,
        .triangleCapacity = allocTriangleCount,
    };

    if (!scratch->emissiveMeshes || !scratch->emissiveTriangles || !scratch->meshAliasQ || !scratch->meshAliasIdx ||
        !scratch->triAliasQ || !scratch->triAliasIdx || !scratch->meshWeights || !scratch->triPmfScratch ||
        !scratch->sourceMeshIndices) {
        LOG_ERROR("Failed to allocate emissive light staging buffers");
        return VKRT_ERROR_OUT_OF_MEMORY;
    }
    return VKRT_SUCCESS;
}

static void freeLightBuildScratch(LightBuildScratch* scratch) {
    if (!scratch) return;

    free(scratch->emissiveMeshes);
    free(scratch->emissiveTriangles);
    free(scratch->meshAliasQ);
    free(scratch->meshAliasIdx);
    free(scratch->triAliasQ);
    free(scratch->triAliasIdx);
    free(scratch->meshWeights);
    free(scratch->triPmfScratch);
    free(scratch->sourceMeshIndices);
    *scratch = (LightBuildScratch){0};
}

static VKRT_Result appendMeshEmissiveTriangles(
    const Mesh* mesh,
    LightBuildScratch* scratch,
    uint32_t* outTriangleOffset,
    uint32_t* outValidTriangleCount,
    float* outTotalArea
) {
    if (!mesh || !scratch || !outTriangleOffset || !outValidTriangleCount || !outTotalArea) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    uint32_t triangleCount = mesh->info.indexCount / 3u;
    VkTransformMatrixKHR worldTransform = getMeshWorldTransform(mesh);
    uint32_t triangleOffset = scratch->emissiveTriangleCount;
    float totalArea = 0.0f;

    for (uint32_t triangleIndex = 0; triangleIndex < triangleCount; triangleIndex++) {
        uint32_t index0 = mesh->indices[(triangleIndex * 3u) + 0u];
        uint32_t index1 = mesh->indices[(triangleIndex * 3u) + 1u];
        uint32_t index2 = mesh->indices[(triangleIndex * 3u) + 2u];
        if (index0 >= mesh->info.vertexCount || index1 >= mesh->info.vertexCount || index2 >= mesh->info.vertexCount) {
            continue;
        }
        if (scratch->emissiveTriangleCount >= scratch->triangleCapacity) {
            LOG_ERROR("Emissive triangle staging overflow");
            return VKRT_ERROR_OPERATION_FAILED;
        }

        vec3 position0;
        vec3 position1;
        vec3 position2;
        transformPosition(&worldTransform, mesh->vertices[index0].position, position0);
        transformPosition(&worldTransform, mesh->vertices[index1].position, position1);
        transformPosition(&worldTransform, mesh->vertices[index2].position, position2);

        vec3 edge1;
        vec3 edge2;
        vec3 crossProduct;
        glm_vec3_sub(position1, position0, edge1);
        glm_vec3_sub(position2, position0, edge2);
        glm_vec3_cross(edge1, edge2, crossProduct);
        float area = 0.5f * glm_vec3_norm(crossProduct);
        if (area <= 0.0f) continue;

        EmissiveTriangle triangleGPU = {0};
        triangleGPU.v0Area[0] = position0[0];
        triangleGPU.v0Area[1] = position0[1];
        triangleGPU.v0Area[2] = position0[2];
        triangleGPU.v0Area[3] = area;
        triangleGPU.e1Pad[0] = edge1[0];
        triangleGPU.e1Pad[1] = edge1[1];
        triangleGPU.e1Pad[2] = edge1[2];
        triangleGPU.e2Pad[0] = edge2[0];
        triangleGPU.e2Pad[1] = edge2[1];
        triangleGPU.e2Pad[2] = edge2[2];

        scratch->emissiveTriangles[scratch->emissiveTriangleCount++] = triangleGPU;
        totalArea += area;
    }

    *outTriangleOffset = triangleOffset;
    *outValidTriangleCount = scratch->emissiveTriangleCount - triangleOffset;
    *outTotalArea = totalArea;
    return VKRT_SUCCESS;
}

static VKRT_Result appendEmissiveMesh(
    VKRT* vkrt,
    uint32_t meshIndex,
    const Material* material,
    LightBuildScratch* scratch
) {
    if (!vkrt || !material || !scratch) return VKRT_ERROR_INVALID_ARGUMENT;

    Mesh* mesh = &vkrt->core.meshes[meshIndex];
    if (scratch->emissiveMeshCount >= scratch->meshCapacity) {
        LOG_ERROR("Emissive mesh staging overflow");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    float emissionWeight = materialEmissionWeight(material);
    uint32_t triangleOffset = 0u;
    uint32_t validTriangleCount = 0u;
    float totalArea = 0.0f;
    VKRT_Result result = appendMeshEmissiveTriangles(mesh, scratch, &triangleOffset, &validTriangleCount, &totalArea);
    if (result != VKRT_SUCCESS) {
        return result;
    }

    float selectionWeight = totalArea * emissionWeight;
    if (selectionWeight <= 0.0f || validTriangleCount == 0u) {
        scratch->emissiveTriangleCount = triangleOffset;
        return VKRT_SUCCESS;
    }

    float invTotalArea = 1.0f / totalArea;
    for (uint32_t triangleIndex = 0; triangleIndex < validTriangleCount; triangleIndex++) {
        scratch->triPmfScratch[triangleIndex] =
            scratch->emissiveTriangles[triangleOffset + triangleIndex].v0Area[3] * invTotalArea;
    }
    if (!buildAliasTable(
            scratch->triPmfScratch,
            validTriangleCount,
            &scratch->triAliasQ[triangleOffset],
            &scratch->triAliasIdx[triangleOffset]
        )) {
        LOG_ERROR("Failed to build emissive triangle alias table");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    EmissiveMesh emissiveMesh = {0};
    emissiveMesh.triOffset = triangleOffset;
    emissiveMesh.triCount = validTriangleCount;
    emissiveMesh.invTotalArea = invTotalArea;
    emissiveMesh.emission[0] = material->emissionColor[0] * material->emissionLuminance;
    emissiveMesh.emission[1] = material->emissionColor[1] * material->emissionLuminance;
    emissiveMesh.emission[2] = material->emissionColor[2] * material->emissionLuminance;

    scratch->meshWeights[scratch->emissiveMeshCount] = selectionWeight;
    scratch->totalSelectionWeight += selectionWeight;
    scratch->sourceMeshIndices[scratch->emissiveMeshCount] = meshIndex;
    scratch->emissiveMeshes[scratch->emissiveMeshCount++] = emissiveMesh;
    return VKRT_SUCCESS;
}

static VKRT_Result populateEmissiveLightScratch(VKRT* vkrt, LightBuildScratch* scratch) {
    if (!vkrt || !scratch) return VKRT_ERROR_INVALID_ARGUMENT;

    for (uint32_t meshIndex = 0; meshIndex < vkrt->core.meshCount; meshIndex++) {
        Mesh* mesh = &vkrt->core.meshes[meshIndex];
        const Material* material = vkrtGetSceneMaterialData(vkrt, mesh->info.materialIndex);
        if (!material) continue;
        if (!materialEligibleForDirectLightSampling(&mesh->info, material)) continue;
        if (materialEmissionWeight(material) <= 0.0f) continue;
        if (!mesh->vertices || !mesh->indices) continue;
        if ((mesh->info.indexCount / 3u) == 0u) continue;

        VKRT_Result result = appendEmissiveMesh(vkrt, meshIndex, material, scratch);
        if (result != VKRT_SUCCESS) {
            return result;
        }
    }
    return VKRT_SUCCESS;
}

static VKRT_Result finalizeMeshSelectionWeights(VKRT* vkrt, LightBuildScratch* scratch) {
    if (!vkrt || !scratch) return VKRT_ERROR_INVALID_ARGUMENT;
    if (scratch->emissiveMeshCount == 0u || scratch->totalSelectionWeight <= 0.0f) {
        return VKRT_SUCCESS;
    }

    float invTotalWeight = 1.0f / scratch->totalSelectionWeight;
    for (uint32_t meshIndex = 0; meshIndex < scratch->emissiveMeshCount; meshIndex++) {
        float pmf = scratch->meshWeights[meshIndex] * invTotalWeight;
        scratch->emissiveMeshes[meshIndex].pmfMesh = pmf;
        scratch->triPmfScratch[meshIndex] = pmf;
        vkrt->core.meshes[scratch->sourceMeshIndices[meshIndex]].info.lightPdfArea =
            pmf * scratch->emissiveMeshes[meshIndex].invTotalArea;
    }
    if (
        !buildAliasTable(scratch->triPmfScratch, scratch->emissiveMeshCount, scratch->meshAliasQ, scratch->meshAliasIdx)
    ) {
        LOG_ERROR("Failed to build emissive mesh alias table");
        return VKRT_ERROR_OPERATION_FAILED;
    }
    return VKRT_SUCCESS;
}

static VKRT_Result uploadScratchLightBuffers(
    VKRT* vkrt,
    const LightBuildScratch* scratch,
    LightBufferState* nextState
) {
    if (!vkrt || !scratch || !nextState) return VKRT_ERROR_INVALID_ARGUMENT;

    uint32_t uploadMeshCount = scratch->emissiveMeshCount > 0u ? scratch->emissiveMeshCount : 1u;
    uint32_t uploadTriangleCount = scratch->emissiveTriangleCount > 0u ? scratch->emissiveTriangleCount : 1u;
    VKRT_Result result = uploadLightBuffer(
        vkrt,
        scratch->emissiveMeshes,
        (VkDeviceSize)uploadMeshCount * sizeof(EmissiveMesh),
        &nextState->sceneEmissiveMeshData
    );
    if (result != VKRT_SUCCESS) return result;
    nextState->sceneEmissiveMeshData.count = scratch->emissiveMeshCount;

    result = uploadLightBuffer(
        vkrt,
        scratch->emissiveTriangles,
        (VkDeviceSize)uploadTriangleCount * sizeof(EmissiveTriangle),
        &nextState->sceneEmissiveTriangleData
    );
    if (result != VKRT_SUCCESS) return result;
    nextState->sceneEmissiveTriangleData.count = scratch->emissiveTriangleCount;

    result = uploadLightBuffer(
        vkrt,
        scratch->meshAliasQ,
        (VkDeviceSize)uploadMeshCount * sizeof(float),
        &nextState->sceneMeshAliasQ
    );
    if (result != VKRT_SUCCESS) return result;
    result = uploadLightBuffer(
        vkrt,
        scratch->meshAliasIdx,
        (VkDeviceSize)uploadMeshCount * sizeof(uint32_t),
        &nextState->sceneMeshAliasIdx
    );
    if (result != VKRT_SUCCESS) return result;
    result = uploadLightBuffer(
        vkrt,
        scratch->triAliasQ,
        (VkDeviceSize)uploadTriangleCount * sizeof(float),
        &nextState->sceneTriAliasQ
    );
    if (result != VKRT_SUCCESS) return result;
    result = uploadLightBuffer(
        vkrt,
        scratch->triAliasIdx,
        (VkDeviceSize)uploadTriangleCount * sizeof(uint32_t),
        &nextState->sceneTriAliasIdx
    );
    if (result != VKRT_SUCCESS) return result;

    nextState->emissiveMeshCount = scratch->emissiveMeshCount;
    nextState->emissiveTriangleCount = scratch->emissiveTriangleCount;
    return VKRT_SUCCESS;
}

VKRT_Result vkrtSceneRebuildLightBuffers(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    EmissiveLightCounts counts = {0};
    VKRT_Result result = countEmissiveLights(vkrt, &counts);
    if (result != VKRT_SUCCESS) {
        return result;
    }

    clearMeshLightPdfAreas(vkrt);

    LightBuildScratch scratch = {0};
    result = allocateLightBuildScratch(&counts, &scratch);
    if (result != VKRT_SUCCESS) {
        freeLightBuildScratch(&scratch);
        return result;
    }

    LightBufferState nextState = {0};
    result = populateEmissiveLightScratch(vkrt, &scratch);
    if (result == VKRT_SUCCESS) {
        result = finalizeMeshSelectionWeights(vkrt, &scratch);
    }
    if (result == VKRT_SUCCESS) {
        result = uploadScratchLightBuffers(vkrt, &scratch, &nextState);
    }

    LightBufferState previousState = {
        .sceneEmissiveMeshData = vkrt->core.sceneEmissiveMeshData,
        .sceneEmissiveTriangleData = vkrt->core.sceneEmissiveTriangleData,
        .sceneMeshAliasQ = vkrt->core.sceneMeshAliasQ,
        .sceneMeshAliasIdx = vkrt->core.sceneMeshAliasIdx,
        .sceneTriAliasQ = vkrt->core.sceneTriAliasQ,
        .sceneTriAliasIdx = vkrt->core.sceneTriAliasIdx,
        .emissiveMeshCount = vkrt->core.emissiveMeshCount,
        .emissiveTriangleCount = vkrt->core.emissiveTriangleCount,
    };
    if (result == VKRT_SUCCESS) {
        applyLightBufferState(vkrt, &nextState);
        destroyLightBufferState(vkrt, &previousState);
        nextState = (LightBufferState){0};
    }

    freeLightBuildScratch(&scratch);
    if (result != VKRT_SUCCESS) {
        destroyLightBufferState(vkrt, &nextState);
    }
    return result;
}
