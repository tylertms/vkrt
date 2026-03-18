#include "lighting.h"

#include "buffer.h"
#include "scene.h"
#include "state.h"
#include "debug.h"

#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

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

static float luminance3(const vec3 value) {
    return value[0] * 0.2126f + value[1] * 0.7152f + value[2] * 0.0722f;
}

static float materialEmissionWeight(const Material* material) {
    if (!isfinite(material->emissionLuminance) || material->emissionLuminance <= 0.0f) return 0.0f;
    float lum = luminance3(material->emissionColor);
    if (lum <= 0.0f) return 0.0f;
    return lum * material->emissionLuminance;
}

static void transformPosition(const VkTransformMatrixKHR* transform, const vec4 position, vec3 outWorld) {
    outWorld[0] = transform->matrix[0][0] * position[0] + transform->matrix[0][1] * position[1] + transform->matrix[0][2] * position[2] + transform->matrix[0][3];
    outWorld[1] = transform->matrix[1][0] * position[0] + transform->matrix[1][1] * position[1] + transform->matrix[1][2] * position[2] + transform->matrix[1][3];
    outWorld[2] = transform->matrix[2][0] * position[0] + transform->matrix[2][1] * position[1] + transform->matrix[2][2] * position[2] + transform->matrix[2][3];
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

    uint32_t smallCount = 0, largeCount = 0;

    for (uint32_t i = 0; i < count; ++i) {
        scaled[i] = pmf[i] * (float)count;
        if (scaled[i] < 1.0f) smallBin[smallCount++] = i;
        else largeBin[largeCount++] = i;
    }

    while (smallCount > 0 && largeCount > 0) {
        uint32_t s = smallBin[--smallCount];
        uint32_t l = largeBin[--largeCount];

        outQ[s] = scaled[s];
        outIdx[s] = l;

        scaled[l] = scaled[l] + scaled[s] - 1.0f;
        if (scaled[l] < 1.0f) smallBin[smallCount++] = l;
        else largeBin[largeCount++] = l;
    }

    while (largeCount > 0) {
        uint32_t l = largeBin[--largeCount];
        outQ[l] = 1.0f;
        outIdx[l] = l;
    }

    while (smallCount > 0) {
        uint32_t s = smallBin[--smallCount];
        outQ[s] = 1.0f;
        outIdx[s] = s;
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

VKRT_Result vkrtSceneRebuildLightBuffers(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    const uint32_t meshCount = vkrt->core.meshCount;
    uint32_t emissiveMeshCount = 0;
    uint32_t emissiveTriangleCount = 0;
    uint64_t emissiveMeshCount64 = 0;
    uint64_t emissiveTriangleCount64 = 0;

    for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++) {
        Mesh* mesh = &vkrt->core.meshes[meshIndex];
        const Material* material = vkrtGetSceneMaterialData(vkrt, mesh->info.materialIndex);
        if (!material || materialEmissionWeight(material) <= 0.0f) continue;
        uint32_t triangleCount = mesh->info.indexCount / 3u;
        if (triangleCount == 0) continue;
        emissiveMeshCount64++;
        emissiveTriangleCount64 += triangleCount;
        if (emissiveMeshCount64 > UINT32_MAX || emissiveTriangleCount64 > UINT32_MAX) {
            LOG_ERROR("Emissive light staging exceeds 32-bit count limits");
            return VKRT_ERROR_OPERATION_FAILED;
        }
    }

    emissiveMeshCount = (uint32_t)emissiveMeshCount64;
    emissiveTriangleCount = (uint32_t)emissiveTriangleCount64;

    for (uint32_t i = 0; i < meshCount; i++) {
        vkrt->core.meshes[i].info.lightPdfArea = 0.0f;
    }

    uint32_t allocMeshCount = emissiveMeshCount > 0 ? emissiveMeshCount : 1u;
    uint32_t allocTriangleCount = emissiveTriangleCount > 0 ? emissiveTriangleCount : 1u;
    if (!canAllocateArray(allocMeshCount, sizeof(EmissiveMesh)) ||
        !canAllocateArray(allocTriangleCount, sizeof(EmissiveTriangle)) ||
        !canAllocateArray(allocMeshCount, sizeof(float)) ||
        !canAllocateArray(allocTriangleCount, sizeof(float)) ||
        !canAllocateArray(allocMeshCount, sizeof(uint32_t)) ||
        !canAllocateArray(allocTriangleCount, sizeof(uint32_t))) {
        LOG_ERROR("Emissive light staging allocation exceeds host address space limits");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    EmissiveMesh* emissiveMeshes = (EmissiveMesh*)calloc(allocMeshCount, sizeof(EmissiveMesh));
    EmissiveTriangle* emissiveTriangles = (EmissiveTriangle*)calloc(allocTriangleCount, sizeof(EmissiveTriangle));
    float* meshAliasQ = (float*)calloc(allocMeshCount, sizeof(float));
    uint32_t* meshAliasIdx = (uint32_t*)calloc(allocMeshCount, sizeof(uint32_t));
    float* triAliasQ = (float*)calloc(allocTriangleCount, sizeof(float));
    uint32_t* triAliasIdx = (uint32_t*)calloc(allocTriangleCount, sizeof(uint32_t));
    float* meshWeights = (float*)calloc(allocMeshCount, sizeof(float));
    float* triPmfScratch = (float*)calloc(allocTriangleCount, sizeof(float));
    uint32_t* sourceMeshIndices = (uint32_t*)calloc(allocMeshCount, sizeof(uint32_t));

    if (!emissiveMeshes || !emissiveTriangles || !meshAliasQ || !meshAliasIdx ||
        !triAliasQ || !triAliasIdx || !meshWeights || !triPmfScratch || !sourceMeshIndices
    ) {
        free(emissiveMeshes); free(emissiveTriangles);
        free(meshAliasQ); free(meshAliasIdx);
        free(triAliasQ); free(triAliasIdx);
        free(meshWeights); free(triPmfScratch);
        free(sourceMeshIndices);
        LOG_ERROR("Failed to allocate emissive light staging buffers");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    float totalSelectionWeight = 0.0f;
    uint32_t meshWriteIndex = 0;
    uint32_t triangleWriteIndex = 0;
    VKRT_Result result = VKRT_SUCCESS;
    LightBufferState nextState = {0};

    for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++) {
        Mesh* mesh = &vkrt->core.meshes[meshIndex];
        const Material* material = vkrtGetSceneMaterialData(vkrt, mesh->info.materialIndex);
        if (!material) continue;
        float emissionWeight = materialEmissionWeight(material);
        if (emissionWeight <= 0.0f) continue;
        if (!mesh->vertices || !mesh->indices) continue;

        uint32_t triangleCount = mesh->info.indexCount / 3u;
        if (triangleCount == 0) continue;

        VkTransformMatrixKHR transform = getMeshTransform(&mesh->info);
        float totalArea = 0.0f;
        uint32_t triangleOffset = triangleWriteIndex;

        for (uint32_t tri = 0; tri < triangleCount; tri++) {
            uint32_t i0 = mesh->indices[tri * 3u + 0u];
            uint32_t i1 = mesh->indices[tri * 3u + 1u];
            uint32_t i2 = mesh->indices[tri * 3u + 2u];
            if (i0 >= mesh->info.vertexCount || i1 >= mesh->info.vertexCount || i2 >= mesh->info.vertexCount) {
                continue;
            }

            vec3 p0, p1, p2;
            transformPosition(&transform, mesh->vertices[i0].position, p0);
            transformPosition(&transform, mesh->vertices[i1].position, p1);
            transformPosition(&transform, mesh->vertices[i2].position, p2);

            vec3 e1, e2, cross;
            glm_vec3_sub(p1, p0, e1);
            glm_vec3_sub(p2, p0, e2);
            glm_vec3_cross(e1, e2, cross);
            float crossLen = glm_vec3_norm(cross);
            float area = 0.5f * crossLen;
            if (area <= 0.0f) continue;

            EmissiveTriangle triGPU = {0};
            triGPU.v0Area[0] = p0[0];
            triGPU.v0Area[1] = p0[1];
            triGPU.v0Area[2] = p0[2];
            triGPU.v0Area[3] = area;
            triGPU.e1Pad[0] = e1[0];
            triGPU.e1Pad[1] = e1[1];
            triGPU.e1Pad[2] = e1[2];
            triGPU.e1Pad[3] = 0.0f;
            triGPU.e2Pad[0] = e2[0];
            triGPU.e2Pad[1] = e2[1];
            triGPU.e2Pad[2] = e2[2];
            triGPU.e2Pad[3] = 0.0f;

            emissiveTriangles[triangleWriteIndex++] = triGPU;
            totalArea += area;
        }

        uint32_t validTriCount = triangleWriteIndex - triangleOffset;
        float selectionWeight = totalArea * emissionWeight;
        if (selectionWeight <= 0.0f || validTriCount == 0) {
            triangleWriteIndex = triangleOffset;
            continue;
        }

        float invTotalArea = 1.0f / totalArea;
        for (uint32_t i = 0; i < validTriCount; i++) {
            triPmfScratch[i] = emissiveTriangles[triangleOffset + i].v0Area[3] * invTotalArea;
        }
        if (!buildAliasTable(triPmfScratch, validTriCount, &triAliasQ[triangleOffset], &triAliasIdx[triangleOffset])) {
            LOG_ERROR("Failed to build emissive triangle alias table");
            result = VKRT_ERROR_OPERATION_FAILED;
            goto cleanup;
        }

        EmissiveMesh meshGPU = {0};
        meshGPU.triOffset = triangleOffset;
        meshGPU.triCount = validTriCount;
        meshGPU.pmfMesh = 0.0f;
        meshGPU.invTotalArea = invTotalArea;
        meshGPU.emission[0] = material->emissionColor[0] * material->emissionLuminance;
        meshGPU.emission[1] = material->emissionColor[1] * material->emissionLuminance;
        meshGPU.emission[2] = material->emissionColor[2] * material->emissionLuminance;
        meshGPU._pad0 = 0.0f;

        meshWeights[meshWriteIndex] = selectionWeight;
        totalSelectionWeight += selectionWeight;
        sourceMeshIndices[meshWriteIndex] = meshIndex;
        emissiveMeshes[meshWriteIndex++] = meshGPU;
    }

    emissiveMeshCount = meshWriteIndex;
    emissiveTriangleCount = triangleWriteIndex;

    if (emissiveMeshCount > 0 && totalSelectionWeight > 0.0f) {
        float invTotalWeight = 1.0f / totalSelectionWeight;
        for (uint32_t i = 0; i < emissiveMeshCount; i++) {
            float pmf = meshWeights[i] * invTotalWeight;
            emissiveMeshes[i].pmfMesh = pmf;
            triPmfScratch[i] = pmf;
            vkrt->core.meshes[sourceMeshIndices[i]].info.lightPdfArea = pmf * emissiveMeshes[i].invTotalArea;
        }
        if (!buildAliasTable(triPmfScratch, emissiveMeshCount, meshAliasQ, meshAliasIdx)) {
            LOG_ERROR("Failed to build emissive mesh alias table");
            result = VKRT_ERROR_OPERATION_FAILED;
            goto cleanup;
        }
    }

    uint32_t uploadMeshCount = emissiveMeshCount > 0 ? emissiveMeshCount : 1u;
    uint32_t uploadTriangleCount = emissiveTriangleCount > 0 ? emissiveTriangleCount : 1u;

    if ((result = uploadLightBuffer(
        vkrt,
        emissiveMeshes,
        (VkDeviceSize)uploadMeshCount * sizeof(EmissiveMesh),
        &nextState.sceneEmissiveMeshData
    )) != VKRT_SUCCESS) goto cleanup;
    nextState.sceneEmissiveMeshData.count = emissiveMeshCount;

    if ((result = uploadLightBuffer(
        vkrt,
        emissiveTriangles,
        (VkDeviceSize)uploadTriangleCount * sizeof(EmissiveTriangle),
        &nextState.sceneEmissiveTriangleData
    )) != VKRT_SUCCESS) goto cleanup;
    nextState.sceneEmissiveTriangleData.count = emissiveTriangleCount;

    if ((result = uploadLightBuffer(
        vkrt,
        meshAliasQ,
        (VkDeviceSize)uploadMeshCount * sizeof(float),
        &nextState.sceneMeshAliasQ
    )) != VKRT_SUCCESS) goto cleanup;

    if ((result = uploadLightBuffer(
        vkrt,
        meshAliasIdx,
        (VkDeviceSize)uploadMeshCount * sizeof(uint32_t),
        &nextState.sceneMeshAliasIdx
    )) != VKRT_SUCCESS) goto cleanup;

    if ((result = uploadLightBuffer(
        vkrt,
        triAliasQ,
        (VkDeviceSize)uploadTriangleCount * sizeof(float),
        &nextState.sceneTriAliasQ
    )) != VKRT_SUCCESS) goto cleanup;

    if ((result = uploadLightBuffer(
        vkrt,
        triAliasIdx,
        (VkDeviceSize)uploadTriangleCount * sizeof(uint32_t),
        &nextState.sceneTriAliasIdx
    )) != VKRT_SUCCESS) goto cleanup;

    nextState.emissiveMeshCount = emissiveMeshCount;
    nextState.emissiveTriangleCount = emissiveTriangleCount;

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
    applyLightBufferState(vkrt, &nextState);
    destroyLightBufferState(vkrt, &previousState);
    nextState = (LightBufferState){0};

cleanup:
    free(emissiveMeshes);
    free(emissiveTriangles);
    free(meshAliasQ);
    free(meshAliasIdx);
    free(triAliasQ);
    free(triAliasIdx);
    free(meshWeights);
    free(triPmfScratch);
    free(sourceMeshIndices);
    if (result != VKRT_SUCCESS) {
        destroyLightBufferState(vkrt, &nextState);
    }
    return result;
}
