#include "geometry.h"

#include "accel/accel.h"
#include "buffer.h"
#include "constants.h"
#include "debug.h"
#include "descriptor.h"
#include "packing.h"
#include "rebuild.h"
#include "scene.h"
#include "state.h"
#include "types.h"
#include "vkrt_engine_types.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <mat4.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const uint64_t kGeometryFingerprintSeed = 1469598103934665603ull;
static const uint64_t kGeometryFingerprintPrime = 1099511628211ull;

static VkBufferUsageFlags verticesUsage(void) {
    return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
}

static VkBufferUsageFlags indicesUsage(void) {
    return VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
           VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
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

VKRT_Result vkrtSceneBuildMeshInfoBuffer(VKRT* vkrt, Buffer* outBuffer) {
    if (!vkrt || !outBuffer) return VKRT_ERROR_INVALID_ARGUMENT;

    *outBuffer = (Buffer){0};
    uint32_t instanceCount = vkrt->core.meshCount;
    outBuffer->count = instanceCount;
    if (instanceCount == 0) {
        return createZeroInitializedDeviceBuffer(
            vkrt,
            sizeof(MeshInfo),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            outBuffer
        );
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
        &outBuffer->buffer,
        &outBuffer->memory,
        &outBuffer->deviceAddress
    );
    free(meshInfos);
    return result;
}

VKRT_Result vkrtSceneRebuildMeshInfoBuffer(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    Buffer nextMeshData = {0};
    VKRT_Result result = vkrtSceneBuildMeshInfoBuffer(vkrt, &nextMeshData);
    if (result != VKRT_SUCCESS) {
        return result;
    }

    Buffer previousMeshData = vkrt->core.sceneMeshData;
    vkrt->core.sceneMeshData = nextMeshData;
    destroyBufferResources(vkrt, &previousMeshData);
    return VKRT_SUCCESS;
}

static void syncDuplicateMeshFromSource(Mesh* mesh, const Mesh* source) {
    if (!mesh || !source) return;
    mesh->vertices = source->vertices;
    mesh->indices = source->indices;
    mesh->geometryFingerprint = source->geometryFingerprint;
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
    vkrtMarkMaterialResourcesDirty(vkrt);
    vkrtMarkSceneResourcesDirty(vkrt);
    vkrtMarkLightResourcesDirty(vkrt);
    markSelectionMaskDirty(vkrt);
    resetSceneData(vkrt);
}

static void shrinkMeshList(VKRT* vkrt, uint32_t meshCount) {
    if (meshCount == 0) {
        free(vkrt->core.meshes);
        vkrt->core.meshes = NULL;
        return;
    }

    Mesh* shrunk = (Mesh*)realloc(vkrt->core.meshes, (size_t)meshCount * sizeof(Mesh));
    if (shrunk) vkrt->core.meshes = shrunk;
}

static uint64_t hashGeometryBytes(uint64_t hash, const void* bytes, size_t byteCount) {
    if (!bytes || byteCount == 0) return hash;

    const uint8_t* cursor = (const uint8_t*)bytes;
    size_t wordCount = byteCount / sizeof(uint64_t);
    const uint64_t* words = (const uint64_t*)cursor;
    for (size_t i = 0; i < wordCount; i++) {
        hash ^= words[i];
        hash *= kGeometryFingerprintPrime;
    }

    size_t remaining = byteCount - (wordCount * sizeof(uint64_t));
    cursor += wordCount * sizeof(uint64_t);
    for (size_t i = 0; i < remaining; i++) {
        hash ^= (uint64_t)cursor[i];
        hash *= kGeometryFingerprintPrime;
    }
    return hash;
}

static uint64_t geometryFingerprint(
    const Vertex* vertices,
    size_t vertexCount,
    const uint32_t* indices,
    size_t indexCount
) {
    uint64_t hash = kGeometryFingerprintSeed;
    hash = hashGeometryBytes(hash, &vertexCount, sizeof(vertexCount));
    hash = hashGeometryBytes(hash, &indexCount, sizeof(indexCount));
    hash = hashGeometryBytes(hash, vertices, vertexCount * sizeof(Vertex));
    hash = hashGeometryBytes(hash, indices, indexCount * sizeof(uint32_t));
    return hash;
}

static uint32_t findDuplicateGeometrySource(
    const VKRT* vkrt,
    uint32_t meshCount,
    const Vertex* vertices,
    size_t vertexCount,
    const uint32_t* indices,
    size_t indexCount,
    uint64_t fingerprint
) {
    if (!vkrt || !vertices || !indices) return VKRT_INVALID_INDEX;

    for (uint32_t i = 0; i < meshCount; i++) {
        const Mesh* existing = &vkrt->core.meshes[i];
        if (!existing->ownsGeometry) continue;
        if (existing->info.vertexCount != (uint32_t)vertexCount || existing->info.indexCount != (uint32_t)indexCount) {
            continue;
        }
        if (existing->geometryFingerprint != fingerprint) {
            continue;
        }
        if (memcmp(existing->vertices, vertices, vertexCount * sizeof(Vertex)) != 0) {
            continue;
        }
        if (memcmp(existing->indices, indices, indexCount * sizeof(uint32_t)) != 0) {
            continue;
        }
        return existing->geometrySource;
    }

    return VKRT_INVALID_INDEX;
}

static void initializeMeshDefaults(Mesh* mesh, size_t vertexCount, size_t indexCount) {
    if (!mesh) return;

    memset(mesh, 0, sizeof(*mesh));
    glm_mat4_identity(mesh->worldTransform);
    mesh->renderBackfacesOverride = -1;
    mesh->hasMaterialAssignment = 0u;
    mesh->info.vertexCount = (uint32_t)vertexCount;
    mesh->info.indexCount = (uint32_t)indexCount;
    mesh->info.materialIndex = 0u;
    mesh->info.renderBackfaces = vkrtResolveMeshRenderBackfaces(mesh);
    mesh->info.opacity = 1.0f;
    float scale[3] = {1.0f, 1.0f, 1.0f};
    memcpy(mesh->info.scale, scale, sizeof(scale));
    memset(&mesh->info.rotation, 0, sizeof(mesh->info.rotation));
    memset(&mesh->info.position, 0, sizeof(mesh->info.position));
}

static void releaseNewMeshRange(VKRT* vkrt, uint32_t startIndex, uint32_t endIndex) {
    if (!vkrt || startIndex > endIndex) return;

    for (uint32_t i = startIndex; i < endIndex; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (!mesh->ownsGeometry) continue;
        free(mesh->vertices);
        free(mesh->indices);
    }
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
    uint32_t selectionEnabled
) {
    if (!vkrt) return;

    if (meshCount > 0 && meshes && vkrt->core.meshes) {
        memcpy(vkrt->core.meshes, meshes, (size_t)meshCount * sizeof(Mesh));
    }
    vkrt->core.meshCount = meshCount;
    vkrt->sceneSettings.selectedMeshIndex = selectedMeshIndex;
    vkrt->sceneSettings.selectionEnabled = selectionEnabled;
}

static void destroyOwnerAccelerationStructures(VKRT* vkrt, GeometryOwnerState* ownerStates, uint32_t ownerCount) {
    if (!vkrt || !ownerStates) return;

    for (uint32_t i = 0; i < ownerCount; i++) {
        vkrtDestroyAccelerationStructureResources(vkrt, &ownerStates[i].accelerationStructure);
    }
}

static VKRT_Result collectGeometryRequirements(
    const VKRT* vkrt,
    uint32_t* outVertexCapacity,
    uint32_t* outIndexCapacity,
    uint32_t* outOwnerCount
) {
    if (!vkrt || !outVertexCapacity || !outIndexCapacity || !outOwnerCount) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    uint64_t requiredVertexCapacity = 0;
    uint64_t requiredIndexCapacity = 0;
    uint32_t ownerCount = 0;
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        const Mesh* mesh = &vkrt->core.meshes[i];
        if (!mesh->ownsGeometry) continue;
        requiredVertexCapacity += mesh->info.vertexCount;
        requiredIndexCapacity += mesh->info.indexCount;
        ownerCount++;
    }

    if (requiredVertexCapacity > VKRT_INVALID_INDEX || requiredIndexCapacity > VKRT_INVALID_INDEX) {
        LOG_ERROR("Geometry layout rebuild exceeds 32-bit buffer limits");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    *outVertexCapacity = (uint32_t)requiredVertexCapacity;
    *outIndexCapacity = (uint32_t)requiredIndexCapacity;
    *outOwnerCount = ownerCount;
    return VKRT_SUCCESS;
}

static Mesh* allocateMeshBackupCopy(const VKRT* vkrt) {
    if (!vkrt || vkrt->core.meshCount == 0) return NULL;

    Mesh* backup = (Mesh*)malloc((size_t)vkrt->core.meshCount * sizeof(Mesh));
    if (!backup) return NULL;

    memcpy(backup, vkrt->core.meshes, (size_t)vkrt->core.meshCount * sizeof(Mesh));
    return backup;
}

static VKRT_Result buildRebuiltGeometryOwners(
    VKRT* vkrt,
    GeometryBufferState* newState,
    GeometryOwnerState* ownerStates,
    uint32_t ownerCount
) {
    if (!vkrt || !newState || (ownerCount > 0 && !ownerStates)) return VKRT_ERROR_INVALID_ARGUMENT;

    uint32_t ownerWriteIndex = 0;
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (!mesh->ownsGeometry) continue;
        if (ownerWriteIndex >= ownerCount) return VKRT_ERROR_OPERATION_FAILED;

        GeometryOwnerState* ownerState = &ownerStates[ownerWriteIndex];
        ownerState->vertexBase = newState->vertexData.count;
        ownerState->indexBase = newState->indexData.count;

        MeshInfo geometryInfo = mesh->info;
        geometryInfo.vertexBase = ownerState->vertexBase;
        geometryInfo.indexBase = ownerState->indexBase;
        VKRT_Result result = createBottomLevelAccelerationStructureForGeometry(
            vkrt,
            &geometryInfo,
            newState->vertexData.deviceAddress,
            newState->indexData.deviceAddress,
            &ownerState->accelerationStructure
        );
        if (result != VKRT_SUCCESS) {
            destroyOwnerAccelerationStructures(vkrt, ownerStates, ownerWriteIndex);
            return VKRT_ERROR_OPERATION_FAILED;
        }

        newState->vertexData.count += mesh->info.vertexCount;
        newState->indexData.count += mesh->info.indexCount;
        ownerWriteIndex++;
    }

    return VKRT_SUCCESS;
}

static void applyRebuiltOwnerGeometry(VKRT* vkrt, const GeometryOwnerState* ownerStates, uint32_t ownerCount) {
    uint32_t ownerReadIndex = 0;
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (!mesh->ownsGeometry) continue;
        if (ownerReadIndex >= ownerCount) return;

        const GeometryOwnerState* ownerState = &ownerStates[ownerReadIndex++];
        mesh->info.vertexBase = ownerState->vertexBase;
        mesh->info.indexBase = ownerState->indexBase;
        mesh->geometrySource = i;
        mesh->geometryUploadPending = 1;
        mesh->blasBuildPending = 1;
        mesh->bottomLevelAccelerationStructure = ownerState->accelerationStructure;
    }
}

static void syncDuplicateGeometryOwners(VKRT* vkrt) {
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (mesh->ownsGeometry) continue;
        mesh->geometrySource = vkrt->core.meshes[mesh->geometrySource].geometrySource;
        syncDuplicateMeshFromSource(mesh, &vkrt->core.meshes[mesh->geometrySource]);
    }
}

static void restorePreviousGeometryState(VKRT* vkrt, const GeometryBufferState* previousState, const Mesh* meshBackup) {
    vkrt->core.vertexData = previousState->vertexData;
    vkrt->core.indexData = previousState->indexData;
    vkrt->core.geometryLayout = previousState->layout;
    if (meshBackup) {
        memcpy(vkrt->core.meshes, meshBackup, (size_t)vkrt->core.meshCount * sizeof(Mesh));
    }
    updateAllDescriptorSets(vkrt);
}

static int allocateMeshHostGeometry(const VKRT_MeshUpload* upload, Mesh* mesh) {
    if (!upload || !mesh || upload->vertexCount == 0 || upload->indexCount == 0) return 0;

    size_t vertexBytes = upload->vertexCount * sizeof(Vertex);
    size_t indexBytes = upload->indexCount * sizeof(uint32_t);
    mesh->vertices = (Vertex*)malloc(vertexBytes);
    mesh->indices = (uint32_t*)malloc(indexBytes);
    if (!mesh->vertices || !mesh->indices) {
        free(mesh->vertices);
        free(mesh->indices);
        mesh->vertices = NULL;
        mesh->indices = NULL;
        return 0;
    }

    memcpy(mesh->vertices, upload->vertices, vertexBytes);
    memcpy(mesh->indices, upload->indices, indexBytes);
    return 1;
}

static int32_t findGeometryPromotionCandidate(const VKRT* vkrt, uint32_t meshIndex) {
    const Mesh* removed = &vkrt->core.meshes[meshIndex];
    if (!removed->ownsGeometry) return -1;

    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
        if (i == meshIndex) continue;

        const Mesh* candidate = &vkrt->core.meshes[i];
        if (candidate->ownsGeometry || candidate->geometrySource != meshIndex) continue;
        return (int32_t)i;
    }

    return -1;
}

static void promoteRemovedGeometryOwner(VKRT* vkrt, int32_t promotedIndex, const Mesh* removed) {
    if (!vkrt || !removed || promotedIndex < 0) return;

    Mesh* promoted = &vkrt->core.meshes[promotedIndex];
    promoted->ownsGeometry = 1;
    promoted->vertices = removed->vertices;
    promoted->indices = removed->indices;
    promoted->bottomLevelAccelerationStructure = removed->bottomLevelAccelerationStructure;
    promoted->geometryUploadPending = removed->geometryUploadPending;
    promoted->blasBuildPending = removed->blasBuildPending;
}

static void removeMeshSlot(VKRT* vkrt, uint32_t meshIndex) {
    uint32_t lastIndex = vkrt->core.meshCount - 1u;
    if (meshIndex != lastIndex) {
        memmove(
            &vkrt->core.meshes[meshIndex],
            &vkrt->core.meshes[meshIndex + 1u],
            (size_t)(lastIndex - meshIndex) * sizeof(Mesh)
        );
    }
    vkrt->core.meshCount = lastIndex;
}

static void remapGeometrySourcesAfterRemoval(VKRT* vkrt, uint32_t meshIndex, int32_t promotedIndex) {
    for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
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
}

static void updateSelectionAfterMeshRemoval(VKRT* vkrt, uint32_t meshIndex) {
    if (vkrt->sceneSettings.selectedMeshIndex == meshIndex) {
        vkrt->sceneSettings.selectedMeshIndex = VKRT_INVALID_INDEX;
    } else if (
        vkrt->sceneSettings.selectedMeshIndex != VKRT_INVALID_INDEX && vkrt->sceneSettings.selectedMeshIndex > meshIndex
    ) {
        vkrt->sceneSettings.selectedMeshIndex--;
    }
    vkrt->sceneSettings.selectionEnabled = vkrt->sceneSettings.selectedMeshIndex != VKRT_INVALID_INDEX;
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
            (VkDeviceSize)vertexCapacity * sizeof(ShaderVertex),
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
    } else {
        VKRT_Result result = createZeroInitializedDeviceBuffer(
            vkrt,
            sizeof(ShaderVertex),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | verticesUsage(),
            &outState->vertexData
        );
        if (result != VKRT_SUCCESS) {
            destroyGeometryBufferState(vkrt, outState);
            return result;
        }
    }

    if (indexCapacity > 0) {
        VKRT_Result result = createBuffer(
            vkrt,
            (VkDeviceSize)indexCapacity * sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | indicesUsage(),
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &outState->indexData.buffer,
            &outState->indexData.memory
        );
        if (result != VKRT_SUCCESS) {
            destroyGeometryBufferState(vkrt, outState);
            return result;
        }
        outState->indexData.deviceAddress = queryBufferDeviceAddress(vkrt, outState->indexData.buffer);
        outState->layout.indexCapacity = indexCapacity;
    } else {
        VKRT_Result result = createZeroInitializedDeviceBuffer(
            vkrt,
            sizeof(uint32_t),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | indicesUsage(),
            &outState->indexData
        );
        if (result != VKRT_SUCCESS) {
            destroyGeometryBufferState(vkrt, outState);
            return result;
        }
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

    uint32_t requiredVertexCapacity = 0;
    uint32_t requiredIndexCapacity = 0;
    uint32_t ownerCount = 0;
    VKRT_Result result =
        collectGeometryRequirements(vkrt, &requiredVertexCapacity, &requiredIndexCapacity, &ownerCount);
    if (result != VKRT_SUCCESS) return result;

    GeometryBufferState newState = {0};
    result = createGeometryBuffers(vkrt, requiredVertexCapacity, requiredIndexCapacity, &newState);
    if (result != VKRT_SUCCESS) return result;

    GeometryOwnerState* ownerStates =
        (GeometryOwnerState*)calloc(ownerCount > 0 ? ownerCount : 1u, sizeof(GeometryOwnerState));
    if (!ownerStates) {
        destroyGeometryBufferState(vkrt, &newState);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    Mesh* meshBackup = allocateMeshBackupCopy(vkrt);
    if (vkrt->core.meshCount > 0 && !meshBackup) {
        free(ownerStates);
        destroyGeometryBufferState(vkrt, &newState);
        return VKRT_ERROR_OPERATION_FAILED;
    }
    result = buildRebuiltGeometryOwners(vkrt, &newState, ownerStates, ownerCount);
    if (result != VKRT_SUCCESS) {
        free(meshBackup);
        free(ownerStates);
        destroyGeometryBufferState(vkrt, &newState);
        return result;
    }

    GeometryBufferState previousState = {
        .vertexData = vkrt->core.vertexData,
        .indexData = vkrt->core.indexData,
        .layout = vkrt->core.geometryLayout,
    };

    vkrt->core.vertexData = newState.vertexData;
    vkrt->core.indexData = newState.indexData;
    vkrt->core.geometryLayout = newState.layout;

    applyRebuiltOwnerGeometry(vkrt, ownerStates, ownerCount);
    syncDuplicateGeometryOwners(vkrt);

    result = updateAllDescriptorSets(vkrt);
    if (result != VKRT_SUCCESS) {
        restorePreviousGeometryState(vkrt, &previousState, meshBackup);
        destroyOwnerAccelerationStructures(vkrt, ownerStates, ownerCount);
        free(meshBackup);
        free(ownerStates);
        destroyGeometryBufferState(vkrt, &newState);
        return result;
    }

    if (meshBackup) {
        for (uint32_t i = 0; i < vkrt->core.meshCount; i++) {
            if (!meshBackup[i].ownsGeometry) continue;
            vkrtDestroyAccelerationStructureResources(vkrt, &meshBackup[i].bottomLevelAccelerationStructure);
        }
    }
    destroyGeometryBufferState(vkrt, &previousState);
    free(meshBackup);
    free(ownerStates);
    return VKRT_SUCCESS;
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

        VkDeviceSize vertexBytes = (VkDeviceSize)mesh->info.vertexCount * sizeof(ShaderVertex);
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
                &upload->stagingMemory
            ) != VKRT_SUCCESS) {
            update->geometryUploadCount = writeIndex + 1u;
            return VKRT_ERROR_OPERATION_FAILED;
        }

        void* mapped = NULL;
        if (vkMapMemory(vkrt->core.device, upload->stagingMemory, 0, stagingSize, 0, &mapped) != VK_SUCCESS ||
            !mapped) {
            update->geometryUploadCount = writeIndex + 1u;
            return VKRT_ERROR_OPERATION_FAILED;
        }
        ShaderVertex* mappedVertices = (ShaderVertex*)mapped;
        for (uint32_t vertexIndex = 0; vertexIndex < mesh->info.vertexCount; vertexIndex++) {
            mappedVertices[vertexIndex] = packShaderVertex(&mesh->vertices[vertexIndex]);
        }
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

    VKRT_MeshUpload upload = {
        .vertices = vertices,
        .vertexCount = vertexCount,
        .indices = indices,
        .indexCount = indexCount,
    };
    VKRT_Result result = vkrtSceneUploadMeshDataBatch(vkrt, &upload, 1u);
    if (result != VKRT_SUCCESS) {
        return result;
    }

    return VKRT_SUCCESS;
}

VKRT_Result vkrtSceneUploadMeshDataBatch(VKRT* vkrt, const VKRT_MeshUpload* uploads, size_t uploadCount) {
    if (!vkrt || !uploads || uploadCount == 0) return VKRT_ERROR_INVALID_ARGUMENT;
    if (uploadCount > (size_t)VKRT_INVALID_INDEX) return VKRT_ERROR_INVALID_ARGUMENT;
    if ((uint64_t)vkrt->core.meshCount + (uint64_t)uploadCount > (uint64_t)VKRT_INVALID_INDEX) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    uint32_t previousCount = vkrt->core.meshCount;
    for (size_t i = 0; i < uploadCount; i++) {
        const VKRT_MeshUpload* upload = &uploads[i];
        if (!upload->vertices || !upload->indices || upload->vertexCount == 0 || upload->indexCount == 0) {
            return VKRT_ERROR_INVALID_ARGUMENT;
        }
        if (upload->vertexCount > VKRT_INVALID_INDEX || upload->indexCount > VKRT_INVALID_INDEX) {
            return VKRT_ERROR_INVALID_ARGUMENT;
        }
    }

    VKRT_Result defaultMaterialResult = vkrtEnsureDefaultMaterial(vkrt);
    if (defaultMaterialResult != VKRT_SUCCESS) {
        return defaultMaterialResult;
    }

    uint32_t newCount = previousCount + (uint32_t)uploadCount;
    Mesh* resized = (Mesh*)realloc(vkrt->core.meshes, (size_t)newCount * sizeof(Mesh));
    if (!resized) {
        LOG_ERROR("Failed to grow mesh list");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->core.meshes = resized;

    for (size_t uploadIndex = 0; uploadIndex < uploadCount; uploadIndex++) {
        const VKRT_MeshUpload* upload = &uploads[uploadIndex];
        uint32_t meshIndex = previousCount + (uint32_t)uploadIndex;
        Mesh* mesh = &vkrt->core.meshes[meshIndex];
        initializeMeshDefaults(mesh, upload->vertexCount, upload->indexCount);

        uint64_t fingerprint =
            geometryFingerprint(upload->vertices, upload->vertexCount, upload->indices, upload->indexCount);
        uint32_t duplicateIndex = findDuplicateGeometrySource(
            vkrt,
            meshIndex,
            upload->vertices,
            upload->vertexCount,
            upload->indices,
            upload->indexCount,
            fingerprint
        );

        if (duplicateIndex == VKRT_INVALID_INDEX) {
            if (!allocateMeshHostGeometry(upload, mesh)) {
                releaseNewMeshRange(vkrt, previousCount, meshIndex);
                shrinkMeshList(vkrt, previousCount);
                LOG_ERROR("Failed to allocate mesh host data");
                return VKRT_ERROR_OPERATION_FAILED;
            }
            mesh->geometryFingerprint = fingerprint;
            mesh->geometrySource = meshIndex;
            mesh->ownsGeometry = 1;
        } else {
            mesh->geometrySource = duplicateIndex;
            mesh->ownsGeometry = 0;
            syncDuplicateMeshFromSource(mesh, &vkrt->core.meshes[duplicateIndex]);
        }
    }

    vkrt->core.meshCount = newCount;
    if (rebuildGeometryLayout(vkrt) != VKRT_SUCCESS) {
        releaseNewMeshRange(vkrt, previousCount, newCount);
        vkrt->core.meshCount = previousCount;
        shrinkMeshList(vkrt, previousCount);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    markSceneMutation(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result vkrtSceneRemoveMesh(VKRT* vkrt, uint32_t meshIndex) {
    if (!vkrt || meshIndex >= vkrt->core.meshCount) return VKRT_ERROR_INVALID_ARGUMENT;

    Mesh* meshBackup = NULL;
    uint32_t meshCountBackup = 0;
    uint32_t selectedMeshIndexBackup = VKRT_INVALID_INDEX;
    uint32_t selectionEnabledBackup = 0;
    if (!backupMeshState(vkrt, &meshBackup, &meshCountBackup, &selectedMeshIndexBackup, &selectionEnabledBackup)) {
        LOG_ERROR("Failed to back up mesh state before removal");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    Mesh removed = vkrt->core.meshes[meshIndex];
    int32_t promotedIndex = findGeometryPromotionCandidate(vkrt, meshIndex);
    promoteRemovedGeometryOwner(vkrt, promotedIndex, &removed);
    removeMeshSlot(vkrt, meshIndex);
    remapGeometrySourcesAfterRemoval(vkrt, meshIndex, promotedIndex);
    updateSelectionAfterMeshRemoval(vkrt, meshIndex);

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

    shrinkMeshList(vkrt, vkrt->core.meshCount);
    free(meshBackup);
    markSceneMutation(vkrt);
    return VKRT_SUCCESS;
}
