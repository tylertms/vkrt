#include "buffer.h"
#include "descriptor.h"
#include "scene.h"
#include "accel.h"
#include "debug.h"
#include "vkrt.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

    mesh->material = (MaterialData){
        .baseColor = {1.0f, 1.0f, 1.0f},
        .roughness = 1.0f,
        .specular = 0.0f,
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

void VKRT_setTimeRange(VKRT* vkrt, float timeBase, float timeStep) {
    if (!vkrt) return;

    vkrt->state.timeBase = timeBase;
    vkrt->state.timeStep = timeStep;
    if (vkrt->core.sceneData) {
        vkrt->core.sceneData->timeBase = timeBase;
        vkrt->core.sceneData->timeStep = timeStep;
    }

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

void VKRT_setRenderViewport(VKRT* vkrt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!vkrt || !vkrt->core.sceneData) return;

    uint32_t fullWidth = vkrt->runtime.swapChainExtent.width;
    uint32_t fullHeight = vkrt->runtime.swapChainExtent.height;

    if (width == 0 || height == 0 || fullWidth == 0 || fullHeight == 0) {
        x = 0;
        y = 0;
        width = fullWidth;
        height = fullHeight;
    }

    if (width <= 1 || height <= 1) {
        x = 0;
        y = 0;
        width = fullWidth;
        height = fullHeight;
    }

    if (x >= fullWidth) x = fullWidth - 1;
    if (y >= fullHeight) y = fullHeight - 1;
    if (x + width > fullWidth) width = fullWidth - x;
    if (y + height > fullHeight) height = fullHeight - y;

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
