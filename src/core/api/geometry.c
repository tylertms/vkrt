#include "buffer.h"
#include "shared.h"
#include "descriptor.h"
#include "debug.h"
#include "scene.h"
#include "accel/accel.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static VkBufferUsageFlags vertexBufferUsage(void) {
    return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
}

static VkBufferUsageFlags indexBufferUsage(void) {
    return VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
           VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
}

static void destroyBufferResources(VKRT* vkrt, Buffer* buffer) {
    if (!vkrt || !buffer) return;
    if (buffer->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, buffer->buffer, NULL);
        buffer->buffer = VK_NULL_HANDLE;
    }
    if (buffer->memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, buffer->memory, NULL);
        buffer->memory = VK_NULL_HANDLE;
    }
    buffer->deviceAddress = 0;
    buffer->count = 0;
}

static void destroyAccelerationStructureResources(VKRT* vkrt, AccelerationStructure* accelerationStructure) {
    if (!vkrt || !accelerationStructure) return;

    if (vkrt->core.procs.vkDestroyAccelerationStructureKHR &&
        accelerationStructure->structure != VK_NULL_HANDLE) {
        vkrt->core.procs.vkDestroyAccelerationStructureKHR(
            vkrt->core.device,
            accelerationStructure->structure,
            NULL);
        accelerationStructure->structure = VK_NULL_HANDLE;
    }
    if (accelerationStructure->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, accelerationStructure->buffer, NULL);
        accelerationStructure->buffer = VK_NULL_HANDLE;
    }
    if (accelerationStructure->memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, accelerationStructure->memory, NULL);
        accelerationStructure->memory = VK_NULL_HANDLE;
    }
    accelerationStructure->deviceAddress = 0;
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

static VKRT_Result createGeometryBuffers(
    VKRT* vkrt,
    uint32_t vertexCapacity,
    uint32_t indexCapacity,
    GeometryBufferState* outState
) {
    if (!vkrt || !outState) return VKRT_ERROR_INVALID_ARGUMENT;

    *outState = (GeometryBufferState){0};
    if (vertexCapacity > 0) {
        VKRT_Result result = createBuffer(
            vkrt,
            (VkDeviceSize)vertexCapacity * sizeof(Vertex),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | vertexBufferUsage(),
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &outState->vertexData.buffer,
            &outState->vertexData.memory);
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
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | indexBufferUsage(),
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

    if (requiredVertexCapacity > UINT32_MAX || requiredIndexCapacity > UINT32_MAX) {
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
                destroyAccelerationStructureResources(vkrt, &ownerStates[ownerIndex].accelerationStructure);
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
            destroyMeshAccelerationStructure(vkrt, mesh);
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

VKRT_Result preparePendingGeometryUploads(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    FrameSceneUpdate* update = &vkrt->runtime.frameSceneUpdates[vkrt->runtime.currentFrame];
    for (uint32_t i = 0; i < update->geometryUploadCount; i++) {
        if (update->geometryUploads[i].stagingBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(vkrt->core.device, update->geometryUploads[i].stagingBuffer, NULL);
        }
        if (update->geometryUploads[i].stagingMemory != VK_NULL_HANDLE) {
            vkFreeMemory(vkrt->core.device, update->geometryUploads[i].stagingMemory, NULL);
        }
    }
    free(update->geometryUploads);
    update->geometryUploads = NULL;
    update->geometryUploadCount = 0;

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

VKRT_Result VKRT_uploadMeshData(
    VKRT* vkrt,
    const Vertex* vertices,
    size_t vertexCount,
    const uint32_t* indices,
    size_t indexCount
) {
    if (!vkrt || !vertices || !indices || vertexCount == 0 || indexCount == 0) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vertexCount > UINT32_MAX || indexCount > UINT32_MAX) return VKRT_ERROR_INVALID_ARGUMENT;

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

    uint32_t duplicateIndex = UINT32_MAX;
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

    if (duplicateIndex == UINT32_MAX) {
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
        destroyMeshAccelerationStructure(vkrt, mesh);
        if (mesh->ownsGeometry) {
            free(mesh->vertices);
            free(mesh->indices);
        }
        vkrt->core.meshCount = previousCount;
        shrinkMeshList(vkrt, previousCount);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    markSceneMutation(vkrt);

    LOG_TRACE("Mesh upload complete. Total Meshes: %u, Vertices: %zu, Indices: %zu, Reused Geometry: %s, in %.3f ms",
        vkrt->core.meshCount,
        vertexCount,
        indexCount,
        duplicateIndex == UINT32_MAX ? "No" : "Yes",
        (double)(getMicroseconds() - startTime) / 1e3);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_removeMesh(VKRT* vkrt, uint32_t meshIndex) {
    if (!vkrt || meshIndex >= vkrt->core.meshCount) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();
    uint32_t meshCount = vkrt->core.meshCount;
    uint32_t lastIndex = meshCount - 1u;
    uint32_t previousSelectedMeshIndex = vkrt->state.selectedMeshIndex;
    Mesh removed = vkrt->core.meshes[meshIndex];
    int32_t promotedIndex = -1;
    Mesh* previousMeshes = (Mesh*)malloc((size_t)meshCount * sizeof(Mesh));
    if (!previousMeshes) {
        LOG_ERROR("Failed to snapshot mesh list before removal");
        return VKRT_ERROR_OPERATION_FAILED;
    }
    memcpy(previousMeshes, vkrt->core.meshes, (size_t)meshCount * sizeof(Mesh));

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
        memmove(&vkrt->core.meshes[meshIndex],
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

    if (vkrt->state.selectedMeshIndex == meshIndex) {
        vkrt->state.selectedMeshIndex = VKRT_INVALID_INDEX;
    } else if (vkrt->state.selectedMeshIndex != VKRT_INVALID_INDEX && vkrt->state.selectedMeshIndex > meshIndex) {
        vkrt->state.selectedMeshIndex--;
    }

    if (rebuildGeometryLayout(vkrt) != VKRT_SUCCESS) {
        memcpy(vkrt->core.meshes, previousMeshes, (size_t)meshCount * sizeof(Mesh));
        vkrt->core.meshCount = meshCount;
        vkrt->state.selectedMeshIndex = previousSelectedMeshIndex;
        free(previousMeshes);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    free(previousMeshes);

    if (removed.ownsGeometry && promotedIndex < 0) {
        destroyAccelerationStructureResources(vkrt, &removed.bottomLevelAccelerationStructure);
        free(removed.vertices);
        free(removed.indices);
    }

    shrinkMeshList(vkrt, lastIndex);
    markSceneMutation(vkrt);

    LOG_TRACE("Mesh removal complete. Removed Index: %u, Remaining Meshes: %u, in %.3f ms",
        meshIndex,
        vkrt->core.meshCount,
        (double)(getMicroseconds() - startTime) / 1e3);
    return VKRT_SUCCESS;
}
