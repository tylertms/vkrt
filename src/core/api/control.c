#include "buffer.h"
#include "command.h"
#include "descriptor.h"
#include "scene.h"
#include "accel.h"
#include "debug.h"
#include "export.h"
#include "vkrt.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

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
    if (!(material->emissionStrength > 0.0f)) return 0.0f;
    float lum = luminance3(material->emissionColor);
    if (!(lum > 0.0f)) return 0.0f;
    return lum * material->emissionStrength;
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

void rebuildLightBuffers(VKRT* vkrt) {
    if (!vkrt) return;

    destroyLightBuffers(vkrt);

    const uint32_t meshCount = vkrt->core.meshData.count;
    uint32_t emissiveMeshCount = 0;
    uint32_t emissiveTriangleCount = 0;

    for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++) {
        Mesh* mesh = &vkrt->core.meshes[meshIndex];
        if (!(materialEmissionWeight(&mesh->material) > 0.0f)) continue;
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
        exit(EXIT_FAILURE);
    }

    float totalSelectionWeight = 0.0f;
    uint32_t meshWriteIndex = 0;
    uint32_t triangleWriteIndex = 0;

    for (uint32_t meshIndex = 0; meshIndex < meshCount; meshIndex++) {
        Mesh* mesh = &vkrt->core.meshes[meshIndex];
        float emissionWeight = materialEmissionWeight(&mesh->material);
        if (!(emissionWeight > 0.0f)) continue;

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
            float area = 0.0f;

            if (i0 < mesh->info.vertexCount && i1 < mesh->info.vertexCount && i2 < mesh->info.vertexCount) {
                transformPosition(&transform, mesh->vertices[i0].position, p0);
                transformPosition(&transform, mesh->vertices[i1].position, p1);
                transformPosition(&transform, mesh->vertices[i2].position, p2);

                vec3 e1Valid, e2Valid, crossE;
                glm_vec3_sub(p1, p0, e1Valid);
                glm_vec3_sub(p2, p0, e2Valid);
                glm_vec3_cross(e1Valid, e2Valid, crossE);
                area = 0.5f * glm_vec3_norm(crossE);
                if (!isfinite(area) || area < 0.0f) area = 0.0f;
                totalArea += area;
            }

            EmissiveTriangleGPU triGPU = {0};
            triGPU.v0Area[0] = p0[0];
            triGPU.v0Area[1] = p0[1];
            triGPU.v0Area[2] = p0[2];
            triGPU.v0Area[3] = area;

            triGPU.e1Pad[0] = p1[0] - p0[0];
            triGPU.e1Pad[1] = p1[1] - p0[1];
            triGPU.e1Pad[2] = p1[2] - p0[2];
            triGPU.e1Pad[3] = 0.0f;

            triGPU.e2Pad[0] = p2[0] - p0[0];
            triGPU.e2Pad[1] = p2[1] - p0[1];
            triGPU.e2Pad[2] = p2[2] - p0[2];
            triGPU.e2Pad[3] = 0.0f;

            emissiveTriangles[triangleWriteIndex++] = triGPU;
        }

        float selectionWeight = totalArea * emissionWeight;
        if (!(selectionWeight > 0.0f)) {
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
        meshGPU.emission[3] = mesh->material.emissionStrength;

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

    vkrt->core.emissiveMeshData.deviceAddress = createBufferFromHostData(
        vkrt,
        emissiveMeshes,
        (VkDeviceSize)uploadMeshCount * sizeof(EmissiveMeshGPU),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &vkrt->core.emissiveMeshData.buffer,
        &vkrt->core.emissiveMeshData.memory
    );
    vkrt->core.emissiveMeshData.count = emissiveMeshCount;

    vkrt->core.emissiveTriangleData.deviceAddress = createBufferFromHostData(
        vkrt,
        emissiveTriangles,
        (VkDeviceSize)uploadTriangleCount * sizeof(EmissiveTriangleGPU),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &vkrt->core.emissiveTriangleData.buffer,
        &vkrt->core.emissiveTriangleData.memory
    );
    vkrt->core.emissiveTriangleData.count = emissiveTriangleCount;

    vkrt->core.emissiveMeshCount = emissiveMeshCount;
    vkrt->core.emissiveTriangleCount = emissiveTriangleCount;
    if (vkrt->core.sceneData) {
        vkrt->core.sceneData->emissiveMeshCount = emissiveMeshCount;
        vkrt->core.sceneData->emissiveTriangleCount = emissiveTriangleCount;
    }

    free(emissiveMeshes);
    free(emissiveTriangles);
}

int VKRT_saveRenderPNG(VKRT* vkrt, const char* path) {
    return saveCurrentRenderPNG(vkrt, path);
}

static int compareTimelineKeyframesByTime(const void* lhs, const void* rhs) {
    const VKRT_SceneTimelineKeyframe* a = (const VKRT_SceneTimelineKeyframe*)lhs;
    const VKRT_SceneTimelineKeyframe* b = (const VKRT_SceneTimelineKeyframe*)rhs;
    if (a->time < b->time) return -1;
    if (a->time > b->time) return 1;
    return 0;
}

static void destroyMeshAccelerationStructure(VKRT* vkrt, Mesh* mesh) {
    if (!vkrt || !vkrt->core.device || !mesh) return;

    PFN_vkDestroyAccelerationStructureKHR destroyAS = vkrt->core.procs.vkDestroyAccelerationStructureKHR;
    if (!destroyAS) return;

    if (mesh->bottomLevelAccelerationStructure.structure != VK_NULL_HANDLE) {
        destroyAS(vkrt->core.device, mesh->bottomLevelAccelerationStructure.structure, NULL);
        mesh->bottomLevelAccelerationStructure.structure = VK_NULL_HANDLE;
    }

    if (mesh->bottomLevelAccelerationStructure.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, mesh->bottomLevelAccelerationStructure.buffer, NULL);
        mesh->bottomLevelAccelerationStructure.buffer = VK_NULL_HANDLE;
    }

    if (mesh->bottomLevelAccelerationStructure.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, mesh->bottomLevelAccelerationStructure.memory, NULL);
        mesh->bottomLevelAccelerationStructure.memory = VK_NULL_HANDLE;
    }

    mesh->bottomLevelAccelerationStructure.deviceAddress = 0;
}

static void destroyMeshBLAS(VKRT* vkrt) {
    if (!vkrt) return;

    for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
        if (vkrt->core.meshes[i].ownsGeometry) {
            destroyMeshAccelerationStructure(vkrt, &vkrt->core.meshes[i]);
        }
    }
}

void rebuildMaterialBuffer(VKRT* vkrt) {
    if (!vkrt) return;

    vkDeviceWaitIdle(vkrt->core.device);

    if (vkrt->core.materialData.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.materialData.buffer, NULL);
        vkrt->core.materialData.buffer = VK_NULL_HANDLE;
    }
    if (vkrt->core.materialData.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, vkrt->core.materialData.memory, NULL);
        vkrt->core.materialData.memory = VK_NULL_HANDLE;
    }

    uint32_t materialCount = vkrt->core.meshData.count;
    vkrt->core.materialData.count = materialCount;
    vkrt->core.materialData.deviceAddress = 0;
    if (materialCount == 0) {
        rebuildLightBuffers(vkrt);
        vkrt->core.materialDataDirty = VK_FALSE;
        return;
    }

    MaterialData* materials = (MaterialData*)malloc((size_t)materialCount * sizeof(MaterialData));
    if (!materials) {
        LOG_ERROR("Failed to allocate material buffer");
        exit(EXIT_FAILURE);
    }

    for (uint32_t i = 0; i < materialCount; i++) {
        vkrt->core.meshes[i].info.materialIndex = i;
        materials[i] = vkrt->core.meshes[i].material;
    }

    vkrt->core.materialData.deviceAddress = createBufferFromHostData(vkrt, materials,
        (VkDeviceSize)materialCount * sizeof(MaterialData),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &vkrt->core.materialData.buffer,
        &vkrt->core.materialData.memory);

    free(materials);
    rebuildLightBuffers(vkrt);
    vkrt->core.materialDataDirty = VK_FALSE;
}

static void rebuildMeshBuffersAndStructures(VKRT* vkrt) {
    if (!vkrt) return;

    uint64_t startTime = getMicroseconds();
    uint64_t waitIdleStartTime = startTime;
    vkDeviceWaitIdle(vkrt->core.device);
    uint64_t waitIdleTime = getMicroseconds() - waitIdleStartTime;

    uint64_t destroyBlasStartTime = getMicroseconds();
    destroyMeshBLAS(vkrt);
    uint64_t destroyBlasTime = getMicroseconds() - destroyBlasStartTime;

    uint64_t destroyBuffersStartTime = getMicroseconds();

    if (vkrt->core.vertexData.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.vertexData.buffer, NULL);
        vkrt->core.vertexData.buffer = VK_NULL_HANDLE;
    }
    if (vkrt->core.vertexData.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, vkrt->core.vertexData.memory, NULL);
        vkrt->core.vertexData.memory = VK_NULL_HANDLE;
    }

    if (vkrt->core.indexData.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, vkrt->core.indexData.buffer, NULL);
        vkrt->core.indexData.buffer = VK_NULL_HANDLE;
    }
    if (vkrt->core.indexData.memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, vkrt->core.indexData.memory, NULL);
        vkrt->core.indexData.memory = VK_NULL_HANDLE;
    }

    vkrt->core.vertexData.count = 0;
    vkrt->core.indexData.count = 0;
    vkrt->core.vertexData.deviceAddress = 0;
    vkrt->core.indexData.deviceAddress = 0;
    uint64_t destroyBuffersTime = getMicroseconds() - destroyBuffersStartTime;

    uint32_t meshCount = vkrt->core.meshData.count;
    uint32_t totalVertexCount = 0;
    uint32_t totalIndexCount = 0;

    for (uint32_t i = 0; i < meshCount; i++) {
        if (!vkrt->core.meshes[i].ownsGeometry) continue;
        totalVertexCount += vkrt->core.meshes[i].info.vertexCount;
        totalIndexCount += vkrt->core.meshes[i].info.indexCount;
    }

    uint64_t packMeshDataTime = 0;
    uint64_t uploadMeshDataTime = 0;
    uint64_t buildBlasTime = 0;
    uint64_t syncInstancesTime = 0;

    if (meshCount > 0) {
        uint64_t packMeshDataStartTime = getMicroseconds();
        Vertex* packedVertices = (Vertex*)malloc((size_t)totalVertexCount * sizeof(Vertex));
        uint32_t* packedIndices = (uint32_t*)malloc((size_t)totalIndexCount * sizeof(uint32_t));
        if (!packedVertices || !packedIndices) {
            free(packedVertices);
            free(packedIndices);
            LOG_ERROR("Failed to allocate packed mesh buffers");
            exit(EXIT_FAILURE);
        }

        uint32_t vertexBase = 0;
        uint32_t indexBase = 0;

        for (uint32_t i = 0; i < meshCount; i++) {
            Mesh* mesh = &vkrt->core.meshes[i];
            if (!mesh->ownsGeometry) continue;

            mesh->geometrySource = i;
            mesh->info.vertexBase = vertexBase;
            mesh->info.indexBase = indexBase;

            memcpy(packedVertices + vertexBase, mesh->vertices, (size_t)mesh->info.vertexCount * sizeof(Vertex));
            memcpy(packedIndices + indexBase, mesh->indices, (size_t)mesh->info.indexCount * sizeof(uint32_t));

            vertexBase += mesh->info.vertexCount;
            indexBase += mesh->info.indexCount;
        }
        packMeshDataTime = getMicroseconds() - packMeshDataStartTime;

        uint64_t uploadMeshDataStartTime = getMicroseconds();
        vkrt->core.vertexData.deviceAddress = createBufferFromHostData(vkrt, packedVertices,
            (VkDeviceSize)totalVertexCount * sizeof(Vertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            &vkrt->core.vertexData.buffer, &vkrt->core.vertexData.memory);

        vkrt->core.indexData.deviceAddress = createBufferFromHostData(vkrt, packedIndices,
            (VkDeviceSize)totalIndexCount * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            &vkrt->core.indexData.buffer, &vkrt->core.indexData.memory);
        uploadMeshDataTime = getMicroseconds() - uploadMeshDataStartTime;

        free(packedVertices);
        free(packedIndices);

        vkrt->core.vertexData.count = totalVertexCount;
        vkrt->core.indexData.count = totalIndexCount;

        uint64_t buildBlasStartTime = getMicroseconds();
        for (uint32_t i = 0; i < meshCount; i++) {
            Mesh* mesh = &vkrt->core.meshes[i];
            if (!mesh->ownsGeometry) continue;
            createBottomLevelAccelerationStructure(vkrt, mesh);
        }
        buildBlasTime = getMicroseconds() - buildBlasStartTime;

        uint64_t syncInstancesStartTime = getMicroseconds();
        for (uint32_t i = 0; i < meshCount; i++) {
            Mesh* mesh = &vkrt->core.meshes[i];
            if (mesh->ownsGeometry) continue;

            Mesh* source = &vkrt->core.meshes[mesh->geometrySource];
            mesh->info.vertexBase = source->info.vertexBase;
            mesh->info.indexBase = source->info.indexBase;
            mesh->bottomLevelAccelerationStructure.deviceAddress = source->bottomLevelAccelerationStructure.deviceAddress;
        }
        syncInstancesTime = getMicroseconds() - syncInstancesStartTime;
    }

    rebuildMaterialBuffer(vkrt);

    uint64_t rebuildTlasStartTime = getMicroseconds();
    createTopLevelAccelerationStructure(vkrt);
    uint64_t rebuildTlasTime = getMicroseconds() - rebuildTlasStartTime;

    uint64_t descriptorUpdateStartTime = getMicroseconds();
    updateDescriptorSet(vkrt);
    uint64_t descriptorUpdateTime = getMicroseconds() - descriptorUpdateStartTime;

    uint64_t resetSceneStartTime = getMicroseconds();
    vkrt->core.topLevelAccelerationStructure.needsRebuild = 0;
    resetSceneData(vkrt);
    uint64_t resetSceneTime = getMicroseconds() - resetSceneStartTime;

    uint32_t uniqueGeometryCount = 0;
    for (uint32_t i = 0; i < meshCount; i++) {
        if (vkrt->core.meshes[i].ownsGeometry) uniqueGeometryCount++;
    }

    LOG_INFO("Scene geometry rebuilt. Meshes: %u, Unique Geometry: %u, Vertices: %u, Indices: %u, in %.3f ms",
        meshCount,
        uniqueGeometryCount,
        totalVertexCount,
        totalIndexCount,
        (double)(getMicroseconds() - startTime) / 1e3);
    LOG_TRACE("Scene geometry rebuild breakdown. Device Wait: %.3f ms, BLAS Cleanup: %.3f ms, Buffer Cleanup: %.3f ms, Data Packing: %.3f ms, Buffer Upload: %.3f ms, BLAS Build: %.3f ms, Instance Sync: %.3f ms, TLAS Build: %.3f ms, Descriptor Update: %.3f ms, Scene Reset: %.3f ms",
        (double)waitIdleTime / 1e3,
        (double)destroyBlasTime / 1e3,
        (double)destroyBuffersTime / 1e3,
        (double)packMeshDataTime / 1e3,
        (double)uploadMeshDataTime / 1e3,
        (double)buildBlasTime / 1e3,
        (double)syncInstancesTime / 1e3,
        (double)rebuildTlasTime / 1e3,
        (double)descriptorUpdateTime / 1e3,
        (double)resetSceneTime / 1e3);
}

void VKRT_uploadMeshData(VKRT* vkrt, const Vertex* vertices, size_t vertexCount, const uint32_t* indices, size_t indexCount) {
    if (!vkrt || !vertices || !indices || vertexCount == 0 || indexCount == 0) return;

    uint64_t startTime = getMicroseconds();
    vkDeviceWaitIdle(vkrt->core.device);

    if (vertexCount > UINT32_MAX || indexCount > UINT32_MAX) {
        LOG_ERROR("Mesh too large");
        return;
    }

    uint32_t newCount = vkrt->core.meshData.count + 1;
    Mesh* resized = (Mesh*)realloc(vkrt->core.meshes, (size_t)newCount * sizeof(Mesh));
    if (!resized) {
        LOG_ERROR("Failed to grow mesh list");
        return;
    }

    vkrt->core.meshes = resized;
    uint32_t newIndex = vkrt->core.meshData.count;
    Mesh* mesh = &vkrt->core.meshes[newIndex];
    memset(mesh, 0, sizeof(*mesh));

    uint32_t duplicateIndex = UINT32_MAX;
    for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
        Mesh* existing = &vkrt->core.meshes[i];
        if (existing->info.vertexCount != (uint32_t)vertexCount || existing->info.indexCount != (uint32_t)indexCount) {
            continue;
        }
        if (memcmp(existing->vertices, vertices, vertexCount * sizeof(Vertex)) != 0) {
            continue;
        }
        if (memcmp(existing->indices, indices, indexCount * sizeof(uint32_t)) != 0) {
            continue;
        }
        duplicateIndex = existing->geometrySource;
        break;
    }

    if (duplicateIndex != UINT32_MAX) {
        Mesh* source = &vkrt->core.meshes[duplicateIndex];
        mesh->vertices = source->vertices;
        mesh->indices = source->indices;
        mesh->geometrySource = duplicateIndex;
        mesh->ownsGeometry = 0;
    } else {
        mesh->vertices = (Vertex*)malloc(vertexCount * sizeof(Vertex));
        mesh->indices = (uint32_t*)malloc(indexCount * sizeof(uint32_t));
        if (!mesh->vertices || !mesh->indices) {
            free(mesh->vertices);
            free(mesh->indices);
            mesh->vertices = NULL;
            mesh->indices = NULL;
            LOG_ERROR("Failed to allocate mesh host data");
            return;
        }

        memcpy(mesh->vertices, vertices, vertexCount * sizeof(Vertex));
        memcpy(mesh->indices, indices, indexCount * sizeof(uint32_t));

        mesh->geometrySource = newIndex;
        mesh->ownsGeometry = 1;
    }

    mesh->info.vertexCount = (uint32_t)vertexCount;
    mesh->info.indexCount = (uint32_t)indexCount;
    mesh->info.materialIndex = newIndex;
    mesh->info.renderBackfaces = 0;
    mesh->info.padding = 0;

    mesh->material = (MaterialData){
        .baseColor = {1.0f, 1.0f, 1.0f},
        .roughness = 1.0f,
        .metallic = 0.0f,
        .emissionColor = {1.0f, 1.0f, 1.0f},
        .emissionStrength = 0.0f,
    };

    vec3 scale = {1.f, 1.f, 1.f};
    memcpy(&mesh->info.scale, &scale, sizeof(vec3));
    memset(&mesh->info.rotation, 0, sizeof(vec3));
    memset(&mesh->info.position, 0, sizeof(vec3));

    vkrt->core.meshData.count = newCount;
    rebuildMeshBuffersAndStructures(vkrt);
    LOG_TRACE("Mesh upload complete. Total Meshes: %u, Vertices: %zu, Indices: %zu, Reused Geometry: %s, in %.3f ms",
        vkrt->core.meshData.count,
        vertexCount,
        indexCount,
        duplicateIndex == UINT32_MAX ? "No" : "Yes",
        (double)(getMicroseconds() - startTime) / 1e3);
}

int VKRT_removeMesh(VKRT* vkrt, uint32_t meshIndex) {
    if (!vkrt || meshIndex >= vkrt->core.meshData.count) return -1;

    uint64_t startTime = getMicroseconds();
    vkDeviceWaitIdle(vkrt->core.device);
    Mesh* removed = &vkrt->core.meshes[meshIndex];

    if (removed->ownsGeometry) {
        int32_t promotedIndex = -1;
        for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
            if (i == meshIndex) continue;

            Mesh* candidate = &vkrt->core.meshes[i];
            if (candidate->ownsGeometry || candidate->geometrySource != meshIndex) continue;

            promotedIndex = (int32_t)i;
            candidate->ownsGeometry = 1;
            candidate->geometrySource = i;
            break;
        }

        if (promotedIndex >= 0) {
            for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
                Mesh* mesh = &vkrt->core.meshes[i];
                if (!mesh->ownsGeometry && mesh->geometrySource == meshIndex) {
                    mesh->geometrySource = (uint32_t)promotedIndex;
                }
            }
        } else {
            free(removed->vertices);
            free(removed->indices);
        }

        destroyMeshAccelerationStructure(vkrt, removed);
    }

    uint32_t last = vkrt->core.meshData.count - 1;
    if (meshIndex != last) {
        memmove(&vkrt->core.meshes[meshIndex], &vkrt->core.meshes[meshIndex + 1], (size_t)(last - meshIndex) * sizeof(Mesh));
    }

    for (uint32_t i = 0; i < last; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (mesh->ownsGeometry) {
            mesh->geometrySource = i;
            continue;
        }

        if (mesh->geometrySource > meshIndex) {
            mesh->geometrySource--;
        }
    }

    vkrt->core.meshData.count = last;
    if (last == 0) {
        free(vkrt->core.meshes);
        vkrt->core.meshes = NULL;
    } else {
        Mesh* shrunk = (Mesh*)realloc(vkrt->core.meshes, (size_t)last * sizeof(Mesh));
        if (shrunk) vkrt->core.meshes = shrunk;
    }

    rebuildMeshBuffersAndStructures(vkrt);
    LOG_TRACE("Mesh removal complete. Removed Index: %u, Remaining Meshes: %u, in %.3f ms",
        meshIndex,
        vkrt->core.meshData.count,
        (double)(getMicroseconds() - startTime) / 1e3);
    return 0;
}

void VKRT_updateTLAS(VKRT* vkrt) {
    if (!vkrt) return;
    vkDeviceWaitIdle(vkrt->core.device);
    if (!vkrt->core.materialDataDirty) {
        rebuildLightBuffers(vkrt);
    }
    createTopLevelAccelerationStructure(vkrt);
    updateDescriptorSet(vkrt);
    resetSceneData(vkrt);
}

void VKRT_applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input) {
    applyCameraInput(vkrt, input);
}

void VKRT_invalidateAccumulation(VKRT* vkrt) {
    if (!vkrt) return;
    resetSceneData(vkrt);
}

void VKRT_setSamplesPerPixel(VKRT* vkrt, uint32_t samplesPerPixel) {
    if (!vkrt) return;
    if (samplesPerPixel == 0) samplesPerPixel = 1;

    if (vkrt->state.samplesPerPixel == samplesPerPixel) return;
    vkrt->state.samplesPerPixel = samplesPerPixel;

    if (vkrt->core.sceneData) {
        vkrt->core.sceneData->samplesPerPixel = samplesPerPixel;
    }
}

void VKRT_setAutoSPPEnabled(VKRT* vkrt, uint8_t enabled) {
    if (!vkrt) return;
    vkrt->state.autoSPPEnabled = enabled ? 1 : 0;
    vkrt->state.autoSPPControlMs = 0.0f;
    vkrt->state.autoSPPFramesUntilNextAdjust = 0;
}

void VKRT_setAutoSPPTargetFPS(VKRT* vkrt, uint32_t targetFPS) {
    if (!vkrt) return;

    if (targetFPS == 0) {
        float hz = vkrt->runtime.displayRefreshHz;
        if (hz <= 0.0f) hz = 60.0f;
        targetFPS = (uint32_t)(hz + 0.5f);
    }

    if (targetFPS < 30) targetFPS = 30;
    if (targetFPS > 360) targetFPS = 360;
    vkrt->state.autoSPPTargetFPS = targetFPS;
    vkrt->state.autoSPPTargetFrameMs = 1000.0f / (float)targetFPS;
    vkrt->state.autoSPPControlMs = 0.0f;
    vkrt->state.autoSPPFramesUntilNextAdjust = 0;
}

void VKRT_setToneMappingMode(VKRT* vkrt, VKRT_ToneMappingMode toneMappingMode) {
    if (!vkrt) return;

    vkrt->state.toneMappingMode = toneMappingMode;
    if (vkrt->core.sceneData) {
        vkrt->core.sceneData->toneMappingMode = toneMappingMode;
    }

    resetSceneData(vkrt);
}

void VKRT_setFogDensity(VKRT* vkrt, float fogDensity) {
    if (!vkrt) return;
    if (!(fogDensity >= 0.0f)) fogDensity = 0.0f;

    if (vkrt->state.fogDensity == fogDensity) return;
    vkrt->state.fogDensity = fogDensity;
    if (vkrt->core.sceneData) {
        vkrt->core.sceneData->fogDensity = fogDensity;
    }

    resetSceneData(vkrt);
}

void VKRT_setDebugMode(VKRT* vkrt, uint32_t mode) {
    if (!vkrt) return;
    if (vkrt->state.debugMode == mode) return;
    vkrt->state.debugMode = mode;
    resetSceneData(vkrt);
}

void VKRT_setNEEEnabled(VKRT* vkrt, uint8_t enabled) {
    if (!vkrt) return;
    uint8_t val = enabled ? 1 : 0;
    if (vkrt->state.neeEnabled == val) return;
    vkrt->state.neeEnabled = val;
    resetSceneData(vkrt);
}

void VKRT_setMISEnabled(VKRT* vkrt, uint8_t enabled) {
    if (!vkrt) return;
    uint8_t val = enabled ? 1 : 0;
    if (vkrt->state.misEnabled == val) return;
    vkrt->state.misEnabled = val;
    resetSceneData(vkrt);
}

void VKRT_setTimeRange(VKRT* vkrt, float timeBase, float timeStep) {
    if (!vkrt) return;

    if (timeBase < 0.0f) {
        timeBase = -1.0f;
        timeStep = -1.0f;
    } else if (timeStep < timeBase) {
        timeStep = timeBase;
    }

    if (vkrt->state.timeBase == timeBase && vkrt->state.timeStep == timeStep) return;

    vkrt->state.timeBase = timeBase;
    vkrt->state.timeStep = timeStep;
    if (vkrt->core.sceneData) {
        vkrt->core.sceneData->timeBase = timeBase;
        vkrt->core.sceneData->timeStep = timeStep;
    }

    resetSceneData(vkrt);
}

void VKRT_setSceneTimeline(VKRT* vkrt, const VKRT_SceneTimelineSettings* timeline) {
    if (!vkrt) return;

    VKRT_SceneTimelineSettings sanitized = {0};

    if (timeline) {
        sanitized.enabled = timeline->enabled ? 1u : 0u;
        uint32_t keyCount = timeline->keyframeCount;
        if (keyCount > VKRT_SCENE_TIMELINE_MAX_KEYFRAMES) {
            keyCount = VKRT_SCENE_TIMELINE_MAX_KEYFRAMES;
        }

        sanitized.keyframeCount = keyCount;
        for (uint32_t keyIndex = 0; keyIndex < keyCount; keyIndex++) {
            VKRT_SceneTimelineKeyframe key = timeline->keyframes[keyIndex];
            if (!isfinite(key.time)) key.time = 0.0f;
            if (!isfinite(key.emissionScale)) key.emissionScale = 1.0f;
            if (key.emissionScale < 0.0f) key.emissionScale = 0.0f;

            for (int channel = 0; channel < 3; channel++) {
                if (!isfinite(key.emissionTint[channel])) key.emissionTint[channel] = 1.0f;
                if (key.emissionTint[channel] < 0.0f) key.emissionTint[channel] = 0.0f;
            }

            sanitized.keyframes[keyIndex] = key;
        }

        if (sanitized.keyframeCount > 1) {
            qsort(sanitized.keyframes,
                sanitized.keyframeCount,
                sizeof(sanitized.keyframes[0]),
                compareTimelineKeyframesByTime);
        }
    }

    if (memcmp(&vkrt->state.sceneTimeline, &sanitized, sizeof(sanitized)) == 0) return;
    vkrt->state.sceneTimeline = sanitized;
    resetSceneData(vkrt);
}

uint32_t VKRT_getMeshCount(const VKRT* vkrt) {
    return vkrt ? vkrt->core.meshData.count : 0;
}

int VKRT_setMeshTransform(VKRT* vkrt, uint32_t meshIndex, vec3 position, vec3 rotation, vec3 scale) {
    if (!vkrt || meshIndex >= vkrt->core.meshData.count) return -1;

    MeshInfo* info = &vkrt->core.meshes[meshIndex].info;
    if (position) glm_vec3_copy(position, info->position);
    if (rotation) glm_vec3_copy(rotation, info->rotation);
    if (scale) glm_vec3_copy(scale, info->scale);

    vkrt->core.topLevelAccelerationStructure.needsRebuild = 1;
    resetSceneData(vkrt);
    return 0;
}

int VKRT_setMeshMaterial(VKRT* vkrt, uint32_t meshIndex, const MaterialData* material) {
    if (!vkrt || !material || meshIndex >= vkrt->core.meshData.count) return -1;

    vkrt->core.meshes[meshIndex].material = *material;
    vkrt->core.materialDataDirty = VK_TRUE;
    resetSceneData(vkrt);
    return 0;
}

int VKRT_setMeshRenderBackfaces(VKRT* vkrt, uint32_t meshIndex, uint8_t renderBackfaces) {
    if (!vkrt || meshIndex >= vkrt->core.meshData.count) return -1;

    uint32_t next = renderBackfaces ? 1u : 0u;
    MeshInfo* info = &vkrt->core.meshes[meshIndex].info;
    if (info->renderBackfaces == next) return 0;

    info->renderBackfaces = next;
    vkrt->core.topLevelAccelerationStructure.needsRebuild = 1;
    resetSceneData(vkrt);
    return 0;
}

static void clampViewportToSwapchain(const VKRT* vkrt, uint32_t* x, uint32_t* y, uint32_t* width, uint32_t* height) {
    if (!vkrt || !x || !y || !width || !height) return;
    uint32_t fullWidth = vkrt->runtime.swapChainExtent.width;
    uint32_t fullHeight = vkrt->runtime.swapChainExtent.height;
    if (fullWidth == 0 || fullHeight == 0) {
        *x = 0;
        *y = 0;
        *width = 0;
        *height = 0;
        return;
    }

    if (*width == 0 || *height == 0) {
        *x = 0;
        *y = 0;
        *width = fullWidth;
        *height = fullHeight;
    }

    if (*width <= 1 || *height <= 1) {
        *x = 0;
        *y = 0;
        *width = fullWidth;
        *height = fullHeight;
    }

    if (*x >= fullWidth) *x = fullWidth - 1;
    if (*y >= fullHeight) *y = fullHeight - 1;
    if (*x + *width > fullWidth) *width = fullWidth - *x;
    if (*y + *height > fullHeight) *height = fullHeight - *y;
}

static void applySceneViewport(VKRT* vkrt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!vkrt || !vkrt->core.sceneData) return;
    uint32_t* rect = vkrt->core.sceneData->viewportRect;
    if (rect[0] == x && rect[1] == y && rect[2] == width && rect[3] == height &&
        vkrt->state.camera.width == width && vkrt->state.camera.height == height) {
        return;
    }

    rect[0] = x;
    rect[1] = y;
    rect[2] = width;
    rect[3] = height;

    vkrt->state.camera.width = width;
    vkrt->state.camera.height = height;
    updateMatricesFromCamera(vkrt);
}

static void recreateRenderTargets(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(vkrt->core.device);
    destroyStorageImage(vkrt);
    createStorageImage(vkrt);
    updateDescriptorSet(vkrt);
}

int VKRT_startRender(VKRT* vkrt, uint32_t width, uint32_t height, uint32_t targetSamples) {
    if (!vkrt || width == 0 || height == 0) return -1;
    if (!vkrt->core.sceneData) return -1;

    if (width > 16384) width = 16384;
    if (height > 16384) height = 16384;

    VkBool32 wasRenderModeActive = vkrt->state.renderModeActive != 0;
    VkBool32 extentChanged = vkrt->runtime.renderExtent.width != width || vkrt->runtime.renderExtent.height != height;

    if (!wasRenderModeActive) {
        vkrt->runtime.savedVsync = vkrt->runtime.vsync;
        if (vkrt->runtime.vsync) {
            vkrt->runtime.vsync = 0;
            vkrt->runtime.framebufferResized = VK_TRUE;
        }
    }
    if (vkrt->state.samplesPerPixel == 0) {
        vkrt->state.samplesPerPixel = 1;
        vkrt->core.sceneData->samplesPerPixel = 1;
    }

    vkrt->runtime.renderExtent = (VkExtent2D){width, height};
    recreateRenderTargets(vkrt);

    vkrt->state.renderModeActive = 1;
    vkrt->state.renderModeFinished = 0;
    vkrt->state.renderTargetSamples = targetSamples;

    if (!wasRenderModeActive || extentChanged) {
        vkrt->state.renderViewZoom = 1.0f;
        vkrt->state.renderViewPanX = 0.0f;
        vkrt->state.renderViewPanY = 0.0f;
        vkrt->state.displayRenderTimeMs = 0.0f;
        vkrt->state.displayFrameTimeMs = 0.0f;
        vkrt->state.lastFrameTimestamp = 0;
        vkrt->state.autoSPPControlMs = 0.0f;
        vkrt->state.autoSPPFramesUntilNextAdjust = 0;
        vkrt->runtime.autoSPPFastAdaptFrames = vkrt->state.autoSPPEnabled ? 8 : 0;
    } else if (!vkrt->state.autoSPPEnabled) {
        vkrt->state.autoSPPControlMs = 0.0f;
        vkrt->state.autoSPPFramesUntilNextAdjust = 0;
        vkrt->runtime.autoSPPFastAdaptFrames = 0;
    }

    applySceneViewport(vkrt, 0, 0, width, height);
    return 0;
}

void VKRT_stopRenderSampling(VKRT* vkrt) {
    if (!vkrt || !vkrt->state.renderModeActive) return;
    vkrt->state.renderModeFinished = 1;
}

static void restoreSavedVsync(VKRT* vkrt) {
    if (vkrt->runtime.vsync != vkrt->runtime.savedVsync) {
        vkrt->runtime.vsync = vkrt->runtime.savedVsync;
        vkrt->runtime.framebufferResized = VK_TRUE;
    }
}

void VKRT_stopRender(VKRT* vkrt) {
    if (!vkrt || !vkrt->state.renderModeActive) return;

    restoreSavedVsync(vkrt);
    vkrt->state.renderModeActive = 0;
    vkrt->state.renderModeFinished = 0;
    vkrt->state.renderTargetSamples = 0;
    vkrt->state.renderViewZoom = 1.0f;
    vkrt->state.renderViewPanX = 0.0f;
    vkrt->state.renderViewPanY = 0.0f;

    vkrt->state.autoSPPControlMs = 0.0f;
    vkrt->state.autoSPPFramesUntilNextAdjust = 0;
    vkrt->runtime.autoSPPFastAdaptFrames = vkrt->state.autoSPPEnabled ? 8 : 0;

    vkrt->runtime.renderExtent = vkrt->runtime.swapChainExtent;
    recreateRenderTargets(vkrt);

    uint32_t x = vkrt->runtime.displayViewportRect[0];
    uint32_t y = vkrt->runtime.displayViewportRect[1];
    uint32_t width = vkrt->runtime.displayViewportRect[2];
    uint32_t height = vkrt->runtime.displayViewportRect[3];
    clampViewportToSwapchain(vkrt, &x, &y, &width, &height);
    applySceneViewport(vkrt, x, y, width, height);
}

void VKRT_setRenderViewport(VKRT* vkrt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!vkrt) return;

    clampViewportToSwapchain(vkrt, &x, &y, &width, &height);
    vkrt->runtime.displayViewportRect[0] = x;
    vkrt->runtime.displayViewportRect[1] = y;
    vkrt->runtime.displayViewportRect[2] = width;
    vkrt->runtime.displayViewportRect[3] = height;

    if (vkrt->state.renderModeActive) return;
    applySceneViewport(vkrt, x, y, width, height);
}

void VKRT_cameraSetPose(VKRT* vkrt, vec3 position, vec3 target, vec3 up, float vfov) {
    if (!vkrt) return;

    if (position) glm_vec3_copy(position, vkrt->state.camera.pos);
    if (target) glm_vec3_copy(target, vkrt->state.camera.target);
    if (up) glm_vec3_copy(up, vkrt->state.camera.up);
    if (vfov > 0.0f) vkrt->state.camera.vfov = vfov;

    updateMatricesFromCamera(vkrt);
}

void VKRT_cameraGetPose(const VKRT* vkrt, vec3 position, vec3 target, vec3 up, float* vfov) {
    if (!vkrt) return;

    if (position) memcpy(position, vkrt->state.camera.pos, sizeof(vec3));
    if (target) memcpy(target, vkrt->state.camera.target, sizeof(vec3));
    if (up) memcpy(up, vkrt->state.camera.up, sizeof(vec3));
    if (vfov) *vfov = vkrt->state.camera.vfov;
}
