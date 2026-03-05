#include "buffer.h"
#include "scene.h"
#include "debug.h"
#include "vkrt_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct EmissiveMeshGPU {
    uint32_t indices[4];
    vec4 emission;
    vec4 stats;
} EmissiveMeshGPU;

typedef struct EmissiveTriangleGPU {
    vec4 v0Area;
    vec4 e1Pad;
    vec4 e2Pad;
} EmissiveTriangleGPU;

static float luminance3(const vec3 value) {
    return value[0] * 0.2126f + value[1] * 0.7152f + value[2] * 0.0722f;
}

static float materialEmissionWeight(const MaterialData* material) {
    if (!material) return 0.0f;
    if (!isfinite(material->emissionLuminance) || material->emissionLuminance <= 0.0f) return 0.0f;
    float lum = luminance3(material->emissionColor);
    if (!isfinite(lum) || lum <= 0.0f) return 0.0f;
    return lum * material->emissionLuminance;
}

static void transformPosition(const VkTransformMatrixKHR* transform, const vec4 position, vec3 outWorld) {
    outWorld[0] = transform->matrix[0][0] * position[0] + transform->matrix[0][1] * position[1] + transform->matrix[0][2] * position[2] + transform->matrix[0][3];
    outWorld[1] = transform->matrix[1][0] * position[0] + transform->matrix[1][1] * position[1] + transform->matrix[1][2] * position[2] + transform->matrix[1][3];
    outWorld[2] = transform->matrix[2][0] * position[0] + transform->matrix[2][1] * position[1] + transform->matrix[2][2] * position[2] + transform->matrix[2][3];
}

static void destroyLightBuffers(VKRT* vkrt) {
    if (!vkrt) return;

    if (vkrt->core.emissiveMeshData.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.emissiveMeshData.buffer, NULL);
        vkrt->core.emissiveMeshData.buffer = VK_NULL_HANDLE;
    }
    if (vkrt->core.emissiveMeshData.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, vkrt->core.emissiveMeshData.memory, NULL);
        vkrt->core.emissiveMeshData.memory = VK_NULL_HANDLE;
    }
    vkrt->core.emissiveMeshData.deviceAddress = 0;
    vkrt->core.emissiveMeshData.count = 0;

    if (vkrt->core.emissiveTriangleData.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.emissiveTriangleData.buffer, NULL);
        vkrt->core.emissiveTriangleData.buffer = VK_NULL_HANDLE;
    }
    if (vkrt->core.emissiveTriangleData.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, vkrt->core.emissiveTriangleData.memory, NULL);
        vkrt->core.emissiveTriangleData.memory = VK_NULL_HANDLE;
    }
    vkrt->core.emissiveTriangleData.deviceAddress = 0;
    vkrt->core.emissiveTriangleData.count = 0;

    vkrt->core.emissiveMeshCount = 0;
    vkrt->core.emissiveTriangleCount = 0;
}

VKRT_Result rebuildLightBuffers(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    destroyLightBuffers(vkrt);

    const uint32_t meshCount = vkrt->core.meshData.count;
    uint32_t emissiveMeshCount = 0;
    uint32_t emissiveTriangleCount = 0;

    for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++) {
        Mesh* mesh = &vkrt->core.meshes[meshIndex];
        if (materialEmissionWeight(&mesh->material) <= 0.0f) continue;
        uint32_t triangleCount = mesh->info.indexCount / 3u;
        if (triangleCount == 0) continue;
        emissiveMeshCount++;
        emissiveTriangleCount += triangleCount;
    }

    uint32_t allocMeshCount = emissiveMeshCount > 0 ? emissiveMeshCount : 1u;
    uint32_t allocTriangleCount = emissiveTriangleCount > 0 ? emissiveTriangleCount : 1u;

    EmissiveMeshGPU* emissiveMeshes = (EmissiveMeshGPU*)calloc(allocMeshCount, sizeof(EmissiveMeshGPU));
    EmissiveTriangleGPU* emissiveTriangles = (EmissiveTriangleGPU*)calloc(allocTriangleCount, sizeof(EmissiveTriangleGPU));
    if (!emissiveMeshes || !emissiveTriangles) {
        free(emissiveMeshes);
        free(emissiveTriangles);
        LOG_ERROR("Failed to allocate emissive light staging buffers");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    float totalSelectionWeight = 0.0f;
    uint32_t meshWriteIndex = 0;
    uint32_t triangleWriteIndex = 0;

    for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++) {
        Mesh* mesh = &vkrt->core.meshes[meshIndex];
        float emissionWeight = materialEmissionWeight(&mesh->material);
        if (emissionWeight <= 0.0f) continue;

        uint32_t triangleCount = mesh->info.indexCount / 3u;
        if (triangleCount == 0) continue;

        VkTransformMatrixKHR transform = getMeshTransform(&mesh->info);
        float totalArea = 0.0f;
        uint32_t triangleOffset = triangleWriteIndex;

        for (uint32_t tri = 0; tri < triangleCount; tri++) {
            uint32_t i0 = mesh->indices[tri * 3u + 0u];
            uint32_t i1 = mesh->indices[tri * 3u + 1u];
            uint32_t i2 = mesh->indices[tri * 3u + 2u];
            vec3 p0 = {0.0f, 0.0f, 0.0f};
            vec3 p1 = {0.0f, 0.0f, 0.0f};
            vec3 p2 = {0.0f, 0.0f, 0.0f};
            vec3 e1World = {0.0f, 0.0f, 0.0f};
            vec3 e2World = {0.0f, 0.0f, 0.0f};
            float area = 0.0f;

            if (i0 < mesh->info.vertexCount && i1 < mesh->info.vertexCount && i2 < mesh->info.vertexCount) {
                transformPosition(&transform, mesh->vertices[i0].position, p0);
                transformPosition(&transform, mesh->vertices[i1].position, p1);
                transformPosition(&transform, mesh->vertices[i2].position, p2);

                glm_vec3_sub(p1, p0, e1World);
                glm_vec3_sub(p2, p0, e2World);

                vec3 crossE;
                glm_vec3_cross(e1World, e2World, crossE);
                area = 0.5f * glm_vec3_norm(crossE);
                if (!isfinite(area) || area < 0.0f) area = 0.0f;
                totalArea += area;

                float* v0p = mesh->vertices[i0].position;
                float* v1p = mesh->vertices[i1].position;
                float* v2p = mesh->vertices[i2].position;
                vec3 e1Obj, e2Obj, objFace;
                glm_vec3_sub(v1p, v0p, e1Obj);
                glm_vec3_sub(v2p, v0p, e2Obj);
                glm_vec3_cross(e1Obj, e2Obj, objFace);

                float* n0 = mesh->vertices[i0].normal;
                float* n1 = mesh->vertices[i1].normal;
                float* n2 = mesh->vertices[i2].normal;
                vec3 avgNormal = {n0[0] + n1[0] + n2[0], n0[1] + n1[1] + n2[1], n0[2] + n1[2] + n2[2]};

                if (glm_vec3_norm2(objFace) > 1e-12f && glm_vec3_norm2(avgNormal) > 1e-12f) {
                    if (glm_vec3_dot(objFace, avgNormal) < 0.0f) {
                        glm_vec3_sub(p2, p0, e1World);
                        glm_vec3_sub(p1, p0, e2World);
                    }
                }
            }

            EmissiveTriangleGPU triGPU = {0};
            triGPU.v0Area[0] = p0[0];
            triGPU.v0Area[1] = p0[1];
            triGPU.v0Area[2] = p0[2];
            triGPU.v0Area[3] = area;

            triGPU.e1Pad[0] = e1World[0];
            triGPU.e1Pad[1] = e1World[1];
            triGPU.e1Pad[2] = e1World[2];
            triGPU.e1Pad[3] = 0.0f;

            triGPU.e2Pad[0] = e2World[0];
            triGPU.e2Pad[1] = e2World[1];
            triGPU.e2Pad[2] = e2World[2];
            triGPU.e2Pad[3] = 0.0f;

            emissiveTriangles[triangleWriteIndex++] = triGPU;
        }

        float selectionWeight = totalArea * emissionWeight;
        if (selectionWeight <= 0.0f) {
            triangleWriteIndex = triangleOffset;
            continue;
        }

        float areaCdf = 0.0f;
        for (uint32_t tri = triangleOffset; tri < triangleWriteIndex; tri++) {
            areaCdf += emissiveTriangles[tri].v0Area[3];
            emissiveTriangles[tri].e1Pad[3] = areaCdf;
        }
        if (triangleWriteIndex > triangleOffset) {
            emissiveTriangles[triangleWriteIndex - 1u].e1Pad[3] = totalArea;
        }

        EmissiveMeshGPU meshGPU = {0};
        meshGPU.indices[0] = meshIndex;
        meshGPU.indices[1] = triangleOffset;
        meshGPU.indices[2] = triangleCount;
        meshGPU.indices[3] = 0;

        meshGPU.emission[0] = mesh->material.emissionColor[0];
        meshGPU.emission[1] = mesh->material.emissionColor[1];
        meshGPU.emission[2] = mesh->material.emissionColor[2];
        meshGPU.emission[3] = mesh->material.emissionLuminance;

        meshGPU.stats[0] = 0.0f;
        meshGPU.stats[1] = totalArea;
        meshGPU.stats[2] = 0.0f;
        meshGPU.stats[3] = selectionWeight;

        emissiveMeshes[meshWriteIndex++] = meshGPU;
        totalSelectionWeight += selectionWeight;
    }

    emissiveMeshCount = meshWriteIndex;
    emissiveTriangleCount = triangleWriteIndex;

    if (emissiveMeshCount > 0) {
        float cumulative = 0.0f;
        if (totalSelectionWeight <= 0.0f || !isfinite(totalSelectionWeight)) {
            float uniform = 1.0f / (float)emissiveMeshCount;
            for (uint32_t i = 0; i < emissiveMeshCount; i++) {
                emissiveMeshes[i].stats[2] = uniform;
                cumulative += uniform;
                emissiveMeshes[i].stats[0] = cumulative;
            }
        } else {
            for (uint32_t i = 0; i < emissiveMeshCount; i++) {
                float p = emissiveMeshes[i].stats[3] / totalSelectionWeight;
                emissiveMeshes[i].stats[2] = p;
                cumulative += p;
                emissiveMeshes[i].stats[0] = cumulative;
            }
        }
        emissiveMeshes[emissiveMeshCount - 1u].stats[0] = 1.0f;
    }

    uint32_t uploadMeshCount = emissiveMeshCount > 0 ? emissiveMeshCount : 1u;
    uint32_t uploadTriangleCount = emissiveTriangleCount > 0 ? emissiveTriangleCount : 1u;

    VKRT_Result result = VKRT_SUCCESS;
    if ((result = createBufferFromHostData(
        vkrt,
        emissiveMeshes,
        (VkDeviceSize)uploadMeshCount * sizeof(EmissiveMeshGPU),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &vkrt->core.emissiveMeshData.buffer,
        &vkrt->core.emissiveMeshData.memory,
        &vkrt->core.emissiveMeshData.deviceAddress)) != VKRT_SUCCESS) {
        free(emissiveMeshes);
        free(emissiveTriangles);
        destroyLightBuffers(vkrt);
        return result;
    }
    vkrt->core.emissiveMeshData.count = emissiveMeshCount;

    if ((result = createBufferFromHostData(
        vkrt,
        emissiveTriangles,
        (VkDeviceSize)uploadTriangleCount * sizeof(EmissiveTriangleGPU),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &vkrt->core.emissiveTriangleData.buffer,
        &vkrt->core.emissiveTriangleData.memory,
        &vkrt->core.emissiveTriangleData.deviceAddress)) != VKRT_SUCCESS) {
        free(emissiveMeshes);
        free(emissiveTriangles);
        destroyLightBuffers(vkrt);
        return result;
    }
    vkrt->core.emissiveTriangleData.count = emissiveTriangleCount;

    vkrt->core.emissiveMeshCount = emissiveMeshCount;
    vkrt->core.emissiveTriangleCount = emissiveTriangleCount;
    if (vkrt->core.sceneData) {
        vkrt->core.sceneData->emissiveMeshCount = emissiveMeshCount;
        vkrt->core.sceneData->emissiveTriangleCount = emissiveTriangleCount;
    }

    free(emissiveMeshes);
    free(emissiveTriangles);
    return VKRT_SUCCESS;
}
