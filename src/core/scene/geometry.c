#include "geometry.h"

#include "rebuild.h"
#include "state.h"
#include "buffer.h"
#include "descriptor.h"
#include "debug.h"
#include "scene.h"
#include "accel/accel.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static VkBufferUsageFlags verticesUsage(void) {
    return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
}

static VkBufferUsageFlags indicesUsage(void) {
    return VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
}

typedef struct GeometryBufferState {
    Buffer vertexData;
    Buffer indexData;
    GeometryLayout layout;
} GeometryBufferState;

typedef struct GeometryOwnerState {
    uint32_t vertexBase;
    uint32_t indexBase;
    AccelerationStructure accelerationStructure;
} GeometryOwnerState;

VKRT_Result vkrtSceneRebuildMeshInfoBuffer(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    Buffer* meshData = &vkrt->core.sceneMeshData;
    destroyBufferResources(vkrt, meshData);

    uint32_t instanceCount = vkrt->core.meshCount;
    meshData->count = instanceCount;
    if (instanceCount == 0) {
        return VKRT_SUCCESS;
    }

    MeshInfo* meshInfos = (MeshInfo*)malloc(sizeof(*meshInfos) * instanceCount);
    if (!meshInfos) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    for (uint32_t i = 0; i < instanceCount; i++) {
        meshInfos[i] = vkrt->core.meshes[i].info;
    }

    VKRT_Result result = createDeviceBufferFromData(
        vkrt,
        meshInfos,
        sizeof(*meshInfos) * instanceCount,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &meshData->buffer,
        &meshData->memory,
        &meshData->deviceAddress);
    free(meshInfos);
    return result;
}

static void syncDuplicateMeshFromSource(Mesh* mesh, const Mesh* source) {
    if (!mesh || !source) return;
    mesh->vertices = source->vertices;
    mesh->indices = source->indices;
    mesh->info.vertexBase = source->info.vertexBase;
    mesh->info.indexBase = source->info.indexBase;
    mesh->bottomLevelAccelerationStructure.deviceAddress = source->bottomLevelAccelerationStructure.deviceAddress;
    mesh->bottomLevelAccelerationStructure.structure = VK_NULL_HANDLE;
    mesh->bottomLevelAccelerationStructure.buffer = VK_NULL_HANDLE;
    mesh->bottomLevelAccelerationStructure.memory = VK_NULL_HANDLE;
    mesh->geometryUploadPending = 0;
    mesh->blasBuildPending = 0;
}

static void resetGeometryLayout(GeometryLayout* layout) {
    if (!layout) return;
    layout->vertexCapacity = 0;
    layout->indexCapacity = 0;
}

static void destroyGeometryBufferState(VKRT* vkrt, GeometryBufferState* state) {
    if (!state) return;
    destroyBufferResources(vkrt, &state->vertexData);
    destroyBufferResources(vkrt, &state->indexData);
    resetGeometryLayout(&state->layout);
}

static void markSceneMutation(VKRT* vkrt) {
    if (!vkrt) return;
    vkrtMarkMaterialResourcesDirty(vkrt);
    vkrtMarkSceneResourcesDirty(vkrt);
    vkrtMarkLightResourcesDirty(vkrt);
    markSelectionMaskDirty(vkrt);
    resetSceneData(vkrt);
}

static void shrinkMeshList(VKRT* vkrt, uint32_t meshCount) {
    if (!vkrt) return;

    if (meshCount == 0) {
        free(vkrt->core.meshes);
        vkrt->core.meshes = NULL;
        return;
    }

    Mesh* shrunk = (Mesh*)realloc(vkrt->core.meshes, (size_t)meshCount * sizeof(Mesh));
    if (shrunk) vkrt->core.meshes = shrunk;
}

static int backupMeshState(
    const VKRT* vkrt,
    Mesh** outMeshes,
    uint32_t* outMeshCount,
    uint32_t* outSelectedMeshIndex,
    uint32_t* outSelectionEnabled
) {
    if (!vkrt || !outMeshes || !outMeshCount || !outSelectedMeshIndex || !outSelectionEnabled) {
        return 0;
    }

    *outMeshes = NULL;
    *outMeshCount = vkrt->core.meshCount;
    *outSelectedMeshIndex = vkrt->sceneSettings.selectedMeshIndex;
    *outSelectionEnabled = vkrt->sceneSettings.selectionEnabled;

    if (vkrt->core.meshCount == 0) return 1;

    Mesh* backup = (Mesh*)malloc((size_t)vkrt->core.meshCount * sizeof(Mesh));
    if (!backup) return 0;

    memcpy(backup, vkrt->core.meshes, (size_t)vkrt->core.meshCount * sizeof(Mesh));
    *outMeshes = backup;
    return 1;
}

static void restoreMeshState(
    VKRT* vkrt,
    const Mesh* meshes,
    uint32_t meshCount,
    uint32_t selectedMeshIndex,
    uint32_t selectionEnabled) {
    if (!vkrt) return;

    if (meshCount > 0 && meshes && vkrt->core.meshes) {
        memcpy(vkrt->core.meshes, meshes, (size_t)meshCount * sizeof(Mesh));
    }
    vkrt->core.meshCount = meshCount;
    vkrt->sceneSettings.selectedMeshIndex = selectedMeshIndex;
    vkrt->sceneSettings.selectionEnabled = selectionEnabled;
}

static VKRT_Result createGeometryBuffers(
    VKRT* vkrt,
    uint32_t vertexCapacity,
    uint32_t indexCapacity,
    GeometryBufferState* outState) {
    if (!vkrt || !outState) return VKRT_ERROR_INVALID_ARGUMENT;

    *outState = (GeometryBufferState){0};
    if (vertexCapacity > 0) {
        VKRT_Result result = createBuffer(
            vkrt,
            (VkDeviceSize)vertexCapacity * sizeof(Vertex),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | verticesUsage(),
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &outState->vertexData.buffer,
            &outState->vertexData.memory
        );
        if (result != VKRT_SUCCESS) {
            destroyGeometryBufferState(vkrt, outState);
            return result;
        }
        outState->vertexData.deviceAddress = queryBufferDeviceAddress(vkrt, outState->vertexData.buffer);
        outState->layout.vertexCapacity = vertexCapacity;
    }

    if (indexCapacity > 0) {
        VKRT_Result result = createBuffer(
            vkrt,
            (VkDeviceSize)indexCapacity * sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | indicesUsage(),
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &outState->indexData.buffer,
            &outState->indexData.memory);
        if (result != VKRT_SUCCESS) {
            destroyGeometryBufferState(vkrt, outState);
            return result;
        }
        outState->indexData.deviceAddress = queryBufferDeviceAddress(vkrt, outState->indexData.buffer);
        outState->layout.indexCapacity = indexCapacity;
    }

    outState->vertexData.count = 0;
    outState->indexData.count = 0;
    return VKRT_SUCCESS;
}

static VKRT_Result rebuildGeometryLayout(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrtWaitForAllInFlightFrames(vkrt) != VKRT_SUCCESS) {
        LOG_ERROR("Failed to wait for in-flight frames before rebuilding geometry layout");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint64_t requiredVertexCapacity = 0;
    uint64_t requiredIndexCapacity = 0;
    uint32_t ownerCount = 0;
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (!mesh->ownsGeometry) continue;
        requiredVertexCapacity += mesh->info.vertexCount;
        requiredIndexCapacity += mesh->info.indexCount;
        ownerCount++;
    }

    if (requiredVertexCapacity > VKRT_INVALID_INDEX || requiredIndexCapacity > VKRT_INVALID_INDEX) {
        LOG_ERROR("Geometry layout rebuild exceeds 32-bit buffer limits");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    GeometryBufferState newState = {0};
    VKRT_Result result = createGeometryBuffers(
        vkrt,
        (uint32_t)requiredVertexCapacity,
        (uint32_t)requiredIndexCapacity,
        &newState);
    if (result != VKRT_SUCCESS) return result;

    GeometryOwnerState* ownerStates = ownerCount > 0
        ? (GeometryOwnerState*)calloc(ownerCount, sizeof(GeometryOwnerState))
        : NULL;
    if (ownerCount > 0 && !ownerStates) {
        destroyGeometryBufferState(vkrt, &newState);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint32_t ownerWriteIndex = 0;
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (!mesh->ownsGeometry) continue;

        GeometryOwnerState* ownerState = &ownerStates[ownerWriteIndex++];
        ownerState->vertexBase = newState.vertexData.count;
        ownerState->indexBase = newState.indexData.count;

        MeshInfo geometryInfo = mesh->info;
        geometryInfo.vertexBase = ownerState->vertexBase;
        geometryInfo.indexBase = ownerState->indexBase;
        result = createBottomLevelAccelerationStructureForGeometry(
            vkrt,
            &geometryInfo,
            newState.vertexData.deviceAddress,
            newState.indexData.deviceAddress,
            &ownerState->accelerationStructure);
        if (result != VKRT_SUCCESS) {
            for (uint32_t ownerIndex = 0; ownerIndex < ownerWriteIndex; ownerIndex++) {
                vkrtDestroyAccelerationStructureResources(vkrt, &ownerStates[ownerIndex].accelerationStructure);
            }
            free(ownerStates);
            destroyGeometryBufferState(vkrt, &newState);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        newState.vertexData.count += mesh->info.vertexCount;
        newState.indexData.count += mesh->info.indexCount;
    }

    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (mesh->ownsGeometry) {
            vkrtDestroyMeshAccelerationStructure(vkrt, mesh);
        }
    }

    destroyBufferResources(vkrt, &vkrt->core.vertexData);
    destroyBufferResources(vkrt, &vkrt->core.indexData);
    vkrt->core.vertexData = newState.vertexData;
    vkrt->core.indexData = newState.indexData;
    vkrt->core.geometryLayout = newState.layout;
    newState = (GeometryBufferState){0};

    ownerWriteIndex = 0;
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (!mesh->ownsGeometry) continue;

        GeometryOwnerState* ownerState = &ownerStates[ownerWriteIndex++];
        mesh->info.vertexBase = ownerState->vertexBase;
        mesh->info.indexBase = ownerState->indexBase;
        mesh->geometrySource = i;
        mesh->geometryUploadPending = 1;
        mesh->blasBuildPending = 1;
        mesh->bottomLevelAccelerationStructure = ownerState->accelerationStructure;
        ownerState->accelerationStructure = (AccelerationStructure){0};
    }

    free(ownerStates);

    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (mesh->ownsGeometry) continue;
        mesh->geometrySource = vkrt->core.meshes[mesh->geometrySource].geometrySource;
        syncDuplicateMeshFromSource(mesh, &vkrt->core.meshes[mesh->geometrySource]);
    }

    return updateAllDescriptorSets(vkrt);
}

VKRT_Result vkrtScenePreparePendingGeometryUploads(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    FrameSceneUpdate* update = vkrtCurrentFrameSceneUpdate(vkrt);
    vkrtCleanupPendingGeometryUploads(vkrt, update);

    uint32_t pendingCount = 0;
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        if (vkrt->core.meshes[i].ownsGeometry && vkrt->core.meshes[i].geometryUploadPending) {
            pendingCount++;
        }
    }

    if (pendingCount == 0) return VKRT_SUCCESS;

    update->geometryUploads = (PendingGeometryUpload*)calloc(pendingCount, sizeof(PendingGeometryUpload));
    if (!update->geometryUploads) return VKRT_ERROR_OPERATION_FAILED;
    update->geometryUploadCount = pendingCount;

    uint32_t writeIndex = 0;
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (!mesh->ownsGeometry || !mesh->geometryUploadPending) continue;

        VkDeviceSize vertexBytes = (VkDeviceSize)mesh->info.vertexCount * sizeof(Vertex);
        VkDeviceSize indexBytes = (VkDeviceSize)mesh->info.indexCount * sizeof(uint32_t);
        VkDeviceSize stagingSize = vertexBytes + indexBytes;

        PendingGeometryUpload* upload = &update->geometryUploads[writeIndex];
        upload->meshIndex = i;
        upload->indexOffset = vertexBytes;
        if (createBuffer(
            vkrt,
            stagingSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &upload->stagingBuffer,
            &upload->stagingMemory) != VKRT_SUCCESS) {
            update->geometryUploadCount = writeIndex + 1u;
            return VKRT_ERROR_OPERATION_FAILED;
        }

        void* mapped = NULL;
        if (vkMapMemory(vkrt->core.device, upload->stagingMemory, 0, stagingSize, 0, &mapped) != VK_SUCCESS || !mapped) {
            update->geometryUploadCount = writeIndex + 1u;
            return VKRT_ERROR_OPERATION_FAILED;
        }
        memcpy(mapped, mesh->vertices, (size_t)vertexBytes);
        memcpy((char*)mapped + vertexBytes, mesh->indices, (size_t)indexBytes);
        vkUnmapMemory(vkrt->core.device, upload->stagingMemory);

        writeIndex++;
    }

    return VKRT_SUCCESS;
}

VKRT_Result vkrtSceneUploadMeshData(
    VKRT* vkrt,
    const Vertex* vertices,
    size_t vertexCount,
    const uint32_t* indices,
    size_t indexCount
) {
    if (!vkrt || !vertices || !indices || vertexCount == 0 || indexCount == 0) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vertexCount > VKRT_INVALID_INDEX || indexCount > VKRT_INVALID_INDEX) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();
    uint32_t previousCount = vkrt->core.meshCount;
    uint32_t newCount = previousCount + 1u;

    Mesh* resized = (Mesh*)realloc(vkrt->core.meshes, (size_t)newCount * sizeof(Mesh));
    if (!resized) {
        LOG_ERROR("Failed to grow mesh list");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->core.meshes = resized;
    Mesh* mesh = &vkrt->core.meshes[previousCount];
    memset(mesh, 0, sizeof(*mesh));
    mesh->renderBackfacesOverride = -1;

    uint32_t duplicateIndex = VKRT_INVALID_INDEX;
    for (uint32_t i = 0; i < previousCount; i++) {
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

    mesh->info.vertexCount = (uint32_t)vertexCount;
    mesh->info.indexCount = (uint32_t)indexCount;
    mesh->info.materialIndex = previousCount;
    mesh->material = VKRT_materialDefault();
    mesh->info.renderBackfaces = vkrtResolveMeshRenderBackfaces(mesh);
    vec3 scale = {1.0f, 1.0f, 1.0f};
    glm_vec3_copy(scale, mesh->info.scale);
    memset(&mesh->info.rotation, 0, sizeof(vec3));
    memset(&mesh->info.position, 0, sizeof(vec3));

    if (duplicateIndex == VKRT_INVALID_INDEX) {
        mesh->vertices = (Vertex*)malloc(vertexCount * sizeof(Vertex));
        mesh->indices = (uint32_t*)malloc(indexCount * sizeof(uint32_t));
        if (!mesh->vertices || !mesh->indices) {
            free(mesh->vertices);
            free(mesh->indices);
            shrinkMeshList(vkrt, previousCount);
            LOG_ERROR("Failed to allocate mesh host data");
            return VKRT_ERROR_OPERATION_FAILED;
        }

        memcpy(mesh->vertices, vertices, vertexCount * sizeof(Vertex));
        memcpy(mesh->indices, indices, indexCount * sizeof(uint32_t));
        mesh->geometrySource = previousCount;
        mesh->ownsGeometry = 1;
    } else {
        Mesh* source = &vkrt->core.meshes[duplicateIndex];
        syncDuplicateMeshFromSource(mesh, source);
        mesh->geometrySource = duplicateIndex;
        mesh->ownsGeometry = 0;
    }

    vkrt->core.meshCount = newCount;
    if (rebuildGeometryLayout(vkrt) != VKRT_SUCCESS) {
        vkrtDestroyMeshAccelerationStructure(vkrt, mesh);
        if (mesh->ownsGeometry) {
            free(mesh->vertices);
            free(mesh->indices);
        }
        vkrt->core.meshCount = previousCount;
        shrinkMeshList(vkrt, previousCount);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    markSceneMutation(vkrt);

    LOG_TRACE(
        "Mesh upload complete. Total Meshes: %u, Vertices: %zu, Indices: %zu, Reused Geometry: %s, in %.3f ms",
        vkrt->core.meshCount,
        vertexCount,
        indexCount,
        duplicateIndex == VKRT_INVALID_INDEX ? "No" : "Yes",
        (double)(getMicroseconds() - startTime) / 1e3
    );
    return VKRT_SUCCESS;
}

VKRT_Result vkrtSceneRemoveMesh(VKRT* vkrt, uint32_t meshIndex) {
    if (!vkrt || meshIndex >= vkrt->core.meshCount) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();
    uint32_t meshCount = vkrt->core.meshCount;
    Mesh* meshBackup = NULL;
    uint32_t meshCountBackup = 0;
    uint32_t selectedMeshIndexBackup = VKRT_INVALID_INDEX;
    uint32_t selectionEnabledBackup = 0;
    if (!backupMeshState(vkrt, &meshBackup, &meshCountBackup, &selectedMeshIndexBackup, &selectionEnabledBackup)) {
        LOG_ERROR("Failed to back up mesh state before removal");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint32_t lastIndex = meshCount - 1u;
    Mesh removed = vkrt->core.meshes[meshIndex];
    int32_t promotedIndex = -1;

    if (removed.ownsGeometry) {
        for (uint32_t i = 0; i < meshCount; i++) {
            if (i == meshIndex) continue;
            Mesh* candidate = &vkrt->core.meshes[i];
            if (candidate->ownsGeometry || candidate->geometrySource != meshIndex) continue;
            promotedIndex = (int32_t)i;
            break;
        }
    }

    if (promotedIndex >= 0) {
        Mesh* promoted = &vkrt->core.meshes[promotedIndex];
        promoted->ownsGeometry = 1;
        promoted->vertices = removed.vertices;
        promoted->indices = removed.indices;
        promoted->bottomLevelAccelerationStructure = removed.bottomLevelAccelerationStructure;
        promoted->geometryUploadPending = removed.geometryUploadPending;
        promoted->blasBuildPending = removed.blasBuildPending;
    }

    if (meshIndex != lastIndex) {
        memmove(
            &vkrt->core.meshes[meshIndex],
            &vkrt->core.meshes[meshIndex + 1u],
            (size_t)(lastIndex - meshIndex) * sizeof(Mesh));
    }

    vkrt->core.meshCount = lastIndex;

    for (uint32_t i = 0; i < lastIndex; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (mesh->ownsGeometry) {
            mesh->geometrySource = i;
            continue;
        }

        if (mesh->geometrySource == meshIndex && promotedIndex >= 0) {
            mesh->geometrySource = (uint32_t)promotedIndex;
        }
        if (mesh->geometrySource > meshIndex) {
            mesh->geometrySource--;
        }

        syncDuplicateMeshFromSource(mesh, &vkrt->core.meshes[mesh->geometrySource]);
    }

    if (vkrt->sceneSettings.selectedMeshIndex == meshIndex) {
        vkrt->sceneSettings.selectedMeshIndex = VKRT_INVALID_INDEX;
    } else if (vkrt->sceneSettings.selectedMeshIndex != VKRT_INVALID_INDEX && vkrt->sceneSettings.selectedMeshIndex > meshIndex) {
        vkrt->sceneSettings.selectedMeshIndex--;
    }
    vkrt->sceneSettings.selectionEnabled = vkrt->sceneSettings.selectedMeshIndex != VKRT_INVALID_INDEX;

    if (rebuildGeometryLayout(vkrt) != VKRT_SUCCESS) {
        restoreMeshState(vkrt, meshBackup, meshCountBackup, selectedMeshIndexBackup, selectionEnabledBackup);
        free(meshBackup);
        LOG_ERROR("Geometry layout rebuild failed after mesh removal");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (removed.ownsGeometry && promotedIndex < 0) {
        vkrtDestroyAccelerationStructureResources(vkrt, &removed.bottomLevelAccelerationStructure);
        free(removed.vertices);
        free(removed.indices);
    }

    shrinkMeshList(vkrt, lastIndex);
    free(meshBackup);
    markSceneMutation(vkrt);

    LOG_TRACE(
        "Mesh removal complete. Removed Index: %u, Remaining Meshes: %u, in %.3f ms",
        meshIndex,
        vkrt->core.meshCount,
        (double)(getMicroseconds() - startTime) / 1e3
    );
    return VKRT_SUCCESS;
}
