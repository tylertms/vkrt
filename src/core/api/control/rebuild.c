#include "buffer.h"
#include "control_internal.h"
#include "descriptor.h"
#include "scene.h"
#include "accel/accel.h"
#include "debug.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void destroyMeshAccelerationStructure(VKRT* vkrt, Mesh* mesh) {
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

VKRT_Result rebuildMaterialBuffer(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

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
        if (rebuildLightBuffers(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
        vkrt->core.materialDataDirty = VK_FALSE;
        return VKRT_SUCCESS;
    }

    MaterialData* materials = (MaterialData*)malloc((size_t)materialCount * sizeof(MaterialData));
    if (!materials) {
        LOG_ERROR("Failed to allocate material buffer");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    for (uint32_t i = 0; i < materialCount; i++) {
        vkrt->core.meshes[i].info.materialIndex = i;
        materials[i] = vkrt->core.meshes[i].material;
    }

    VKRT_Result result = VKRT_SUCCESS;
    if ((result = createBufferFromHostData(vkrt,
        materials,
        (VkDeviceSize)materialCount * sizeof(MaterialData),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &vkrt->core.materialData.buffer,
        &vkrt->core.materialData.memory,
        &vkrt->core.materialData.deviceAddress)) != VKRT_SUCCESS) {
        free(materials);
        return result;
    }
    free(materials);

    if ((result = rebuildLightBuffers(vkrt)) != VKRT_SUCCESS) return result;
    vkrt->core.materialDataDirty = VK_FALSE;
    return VKRT_SUCCESS;
}

VKRT_Result rebuildSceneGeometry(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

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
            return VKRT_ERROR_OPERATION_FAILED;
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
        VKRT_Result result = VKRT_SUCCESS;
        if ((result = createBufferFromHostData(vkrt,
            packedVertices,
            (VkDeviceSize)totalVertexCount * sizeof(Vertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            &vkrt->core.vertexData.buffer,
            &vkrt->core.vertexData.memory,
            &vkrt->core.vertexData.deviceAddress)) != VKRT_SUCCESS) {
            free(packedVertices);
            free(packedIndices);
            return result;
        }

        if ((result = createBufferFromHostData(vkrt,
            packedIndices,
            (VkDeviceSize)totalIndexCount * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            &vkrt->core.indexData.buffer,
            &vkrt->core.indexData.memory,
            &vkrt->core.indexData.deviceAddress)) != VKRT_SUCCESS) {
            free(packedVertices);
            free(packedIndices);
            if (vkrt->core.vertexData.buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(vkrt->core.device, vkrt->core.vertexData.buffer, NULL);
                vkrt->core.vertexData.buffer = VK_NULL_HANDLE;
            }
            if (vkrt->core.vertexData.memory != VK_NULL_HANDLE) {
                vkFreeMemory(vkrt->core.device, vkrt->core.vertexData.memory, NULL);
                vkrt->core.vertexData.memory = VK_NULL_HANDLE;
            }
            vkrt->core.vertexData.deviceAddress = 0;
            return result;
        }
        uploadMeshDataTime = getMicroseconds() - uploadMeshDataStartTime;

        free(packedVertices);
        free(packedIndices);

        vkrt->core.vertexData.count = totalVertexCount;
        vkrt->core.indexData.count = totalIndexCount;

        uint64_t buildBlasStartTime = getMicroseconds();
        if ((result = buildAllBLAS(vkrt)) != VKRT_SUCCESS) return result;
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

    VKRT_Result result = VKRT_SUCCESS;
    if ((result = rebuildMaterialBuffer(vkrt)) != VKRT_SUCCESS) return result;

    uint64_t rebuildTlasStartTime = getMicroseconds();
    if ((result = createTopLevelAccelerationStructure(vkrt)) != VKRT_SUCCESS) return result;
    uint64_t rebuildTlasTime = getMicroseconds() - rebuildTlasStartTime;

    uint64_t descriptorUpdateStartTime = getMicroseconds();
    if ((result = updateDescriptorSet(vkrt)) != VKRT_SUCCESS) return result;
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
    return VKRT_SUCCESS;
}

VKRT_Result rebuildTopLevelScene(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    vkDeviceWaitIdle(vkrt->core.device);
    if (!vkrt->core.materialDataDirty) {
        if (rebuildLightBuffers(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    }
    VKRT_Result result = VKRT_SUCCESS;
    if ((result = createTopLevelAccelerationStructure(vkrt)) != VKRT_SUCCESS) return result;
    if ((result = updateDescriptorSet(vkrt)) != VKRT_SUCCESS) return result;
    vkrt->core.topLevelAccelerationStructure.needsRebuild = 0;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_updateTLAS(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    return rebuildTopLevelScene(vkrt);
}
