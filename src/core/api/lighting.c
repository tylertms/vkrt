#include "buffer.h"
#include "scene.h"
#include "debug.h"
#include "vkrt_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static float luminance3(const vec3 value) {
    return value[0] * 0.2126f + value[1] * 0.7152f + value[2] * 0.0722f;
}

static float materialEmissionWeight(const Material* material) {
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

static Buffer* getEmissiveMeshData(VKRT* vkrt) {
    return &vkrt->core.sceneEmissiveMeshData;
}

static Buffer* getEmissiveTriangleData(VKRT* vkrt) {
    return &vkrt->core.sceneEmissiveTriangleData;
}

static void destroyLightBuffers(VKRT* vkrt) {
    if (!vkrt) return;

    destroyBufferResources(vkrt, getEmissiveMeshData(vkrt));
    destroyBufferResources(vkrt, getEmissiveTriangleData(vkrt));

    vkrt->core.emissiveMeshCount = 0;
    vkrt->core.emissiveTriangleCount = 0;
}

VKRT_Result rebuildLightBuffers(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    destroyLightBuffers(vkrt);

    const uint32_t meshCount = vkrt->core.meshCount;
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

    EmissiveMesh* emissiveMeshes = (EmissiveMesh*)calloc(allocMeshCount, sizeof(EmissiveMesh));
    EmissiveTriangle* emissiveTriangles = (EmissiveTriangle*)calloc(allocTriangleCount, sizeof(EmissiveTriangle));
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
        vec3 boundsMin = {INFINITY, INFINITY, INFINITY};
        vec3 boundsMax = {-INFINITY, -INFINITY, -INFINITY};

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

                for (int c = 0; c < 3; c++) {
                    if (p0[c] < boundsMin[c]) boundsMin[c] = p0[c];
                    if (p1[c] < boundsMin[c]) boundsMin[c] = p1[c];
                    if (p2[c] < boundsMin[c]) boundsMin[c] = p2[c];
                    if (p0[c] > boundsMax[c]) boundsMax[c] = p0[c];
                    if (p1[c] > boundsMax[c]) boundsMax[c] = p1[c];
                    if (p2[c] > boundsMax[c]) boundsMax[c] = p2[c];
                }

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

            EmissiveTriangle triGPU = {0};
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

        vec3 boundsCenter = {
            0.5f * (boundsMin[0] + boundsMax[0]),
            0.5f * (boundsMin[1] + boundsMax[1]),
            0.5f * (boundsMin[2] + boundsMax[2]),
        };
        float boundsRadius = 0.0f;
        for (uint32_t tri = triangleOffset; tri < triangleWriteIndex; tri++) {
            vec3 p0 = {
                emissiveTriangles[tri].v0Area[0],
                emissiveTriangles[tri].v0Area[1],
                emissiveTriangles[tri].v0Area[2],
            };
            vec3 p1 = {
                p0[0] + emissiveTriangles[tri].e1Pad[0],
                p0[1] + emissiveTriangles[tri].e1Pad[1],
                p0[2] + emissiveTriangles[tri].e1Pad[2],
            };
            vec3 p2 = {
                p0[0] + emissiveTriangles[tri].e2Pad[0],
                p0[1] + emissiveTriangles[tri].e2Pad[1],
                p0[2] + emissiveTriangles[tri].e2Pad[2],
            };
            vec3 offset = {0.0f, 0.0f, 0.0f};

            glm_vec3_sub(p0, boundsCenter, offset);
            boundsRadius = fmaxf(boundsRadius, glm_vec3_norm(offset));
            glm_vec3_sub(p1, boundsCenter, offset);
            boundsRadius = fmaxf(boundsRadius, glm_vec3_norm(offset));
            glm_vec3_sub(p2, boundsCenter, offset);
            boundsRadius = fmaxf(boundsRadius, glm_vec3_norm(offset));
        }

        float areaCdf = 0.0f;
        for (uint32_t tri = triangleOffset; tri < triangleWriteIndex; tri++) {
            areaCdf += emissiveTriangles[tri].v0Area[3];
            emissiveTriangles[tri].e1Pad[3] = areaCdf;
        }
        if (triangleWriteIndex > triangleOffset) {
            emissiveTriangles[triangleWriteIndex - 1u].e1Pad[3] = totalArea;
        }

        EmissiveMesh meshGPU = {0};
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
        meshGPU.bounds[0] = boundsCenter[0];
        meshGPU.bounds[1] = boundsCenter[1];
        meshGPU.bounds[2] = boundsCenter[2];
        meshGPU.bounds[3] = boundsRadius;

        emissiveMeshes[meshWriteIndex++] = meshGPU;
        totalSelectionWeight += selectionWeight;
    }

    emissiveMeshCount = meshWriteIndex;
    emissiveTriangleCount = triangleWriteIndex;

    if (emissiveMeshCount > 0 && totalSelectionWeight > 0.0f) {
        float selectionCdf = 0.0f;
        for (uint32_t meshIndex = 0; meshIndex < emissiveMeshCount; meshIndex++) {
            float selectionPdf = emissiveMeshes[meshIndex].stats[3] / totalSelectionWeight;
            emissiveMeshes[meshIndex].stats[0] = selectionPdf;
            selectionCdf += selectionPdf;
            emissiveMeshes[meshIndex].stats[2] = selectionCdf;
        }
        emissiveMeshes[emissiveMeshCount - 1u].stats[2] = 1.0f;
    }

    uint32_t uploadMeshCount = emissiveMeshCount > 0 ? emissiveMeshCount : 1u;
    uint32_t uploadTriangleCount = emissiveTriangleCount > 0 ? emissiveTriangleCount : 1u;

    VKRT_Result result = VKRT_SUCCESS;
    Buffer* emissiveMeshData = getEmissiveMeshData(vkrt);
    Buffer* emissiveTriangleData = getEmissiveTriangleData(vkrt);
    if ((result = createDeviceBufferFromData(
        vkrt,
        emissiveMeshes,
        (VkDeviceSize)uploadMeshCount * sizeof(EmissiveMesh),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &emissiveMeshData->buffer,
        &emissiveMeshData->memory,
        &emissiveMeshData->deviceAddress)) != VKRT_SUCCESS) {
        free(emissiveMeshes);
        free(emissiveTriangles);
        destroyLightBuffers(vkrt);
        return result;
    }
    emissiveMeshData->count = emissiveMeshCount;

    if ((result = createDeviceBufferFromData(
        vkrt,
        emissiveTriangles,
        (VkDeviceSize)uploadTriangleCount * sizeof(EmissiveTriangle),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &emissiveTriangleData->buffer,
        &emissiveTriangleData->memory,
        &emissiveTriangleData->deviceAddress)) != VKRT_SUCCESS) {
        free(emissiveMeshes);
        free(emissiveTriangles);
        destroyLightBuffers(vkrt);
        return result;
    }
    emissiveTriangleData->count = emissiveTriangleCount;

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
