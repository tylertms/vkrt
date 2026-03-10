#include "rebuild.h"

#include "lighting.h"
#include "state.h"
#include "buffer.h"
#include "accel/accel.h"
#include "debug.h"

#include <stdint.h>
#include <stdlib.h>

static Buffer* getMaterialData(VKRT* vkrt) {
    return &vkrt->core.sceneMaterialData;
}

void vkrtCleanupPendingGeometryUploads(VKRT* vkrt, FrameSceneUpdate* update) {
    if (!vkrt || !update) return;
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
}

void vkrtCleanupPendingBLASBuilds(VKRT* vkrt, FrameSceneUpdate* update) {
    if (!vkrt || !update) return;
    for (uint32_t i = 0; i < update->blasBuildCount; i++) {
        if (update->blasBuilds[i].scratchBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(vkrt->core.device, update->blasBuilds[i].scratchBuffer, NULL);
        }
        if (update->blasBuilds[i].scratchMemory != VK_NULL_HANDLE) {
            vkFreeMemory(vkrt->core.device, update->blasBuilds[i].scratchMemory, NULL);
        }
    }
    free(update->blasBuilds);
    update->blasBuilds = NULL;
    update->blasBuildCount = 0;
}

void vkrtCleanupFrameSceneUpdate(VKRT* vkrt, uint32_t frameIndex) {
    if (!vkrt || frameIndex >= VKRT_MAX_FRAMES_IN_FLIGHT) return;

    FrameSceneUpdate* update = &vkrt->runtime.frameSceneUpdates[frameIndex];

    for (uint32_t i = 0; i < update->sceneTransferCount; i++) {
        if (update->sceneTransfers[i].stagingBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(vkrt->core.device, update->sceneTransfers[i].stagingBuffer, NULL);
        }
        if (update->sceneTransfers[i].stagingMemory != VK_NULL_HANDLE) {
            vkFreeMemory(vkrt->core.device, update->sceneTransfers[i].stagingMemory, NULL);
        }
    }
    free(update->sceneTransfers);
    update->sceneTransfers = NULL;
    update->sceneTransferCount = 0;

    vkrtCleanupPendingGeometryUploads(vkrt, update);
    vkrtCleanupPendingBLASBuilds(vkrt, update);

    destroyTransfer(vkrt, &update->instanceBuffer);
    destroyTransfer(vkrt, &update->tlasScratch);

    update->tlasBuildPending = VK_FALSE;
}

void vkrtDestroyMeshAccelerationStructure(VKRT* vkrt, Mesh* mesh) {
    if (!mesh) return;
    vkrtDestroyAccelerationStructureResources(vkrt, &mesh->bottomLevelAccelerationStructure);
}

VKRT_Result vkrtSceneRebuildMaterialBuffer(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    Buffer* materialData = getMaterialData(vkrt);
    destroyBufferResources(vkrt, materialData);

    uint32_t materialCount = vkrt->core.meshCount;
    materialData->count = materialCount;
    if (materialCount == 0) {
        return vkrtSceneRebuildLightBuffers(vkrt);
    }

    Material* materials = (Material*)malloc((size_t)materialCount * sizeof(Material));
    if (!materials) {
        LOG_ERROR("Failed to allocate material buffer");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    for (uint32_t i = 0; i < materialCount; i++) {
        vkrt->core.meshes[i].info.materialIndex = i;
        materials[i] = vkrt->core.meshes[i].material;
    }

    VKRT_Result result = createDeviceBufferFromData(
        vkrt,
        materials,
        (VkDeviceSize)materialCount * sizeof(Material),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &materialData->buffer,
        &materialData->memory,
        &materialData->deviceAddress);
    free(materials);
    if (result != VKRT_SUCCESS) {
        return result;
    }

    if ((result = vkrtSceneRebuildLightBuffers(vkrt)) != VKRT_SUCCESS) {
        return result;
    }

    return VKRT_SUCCESS;
}

VKRT_Result vkrtSceneRebuildTopLevelScene(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    return createTopLevelAccelerationStructure(vkrt);
}
