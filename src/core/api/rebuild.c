#include "buffer.h"
#include "shared.h"
#include "scene.h"
#include "accel/accel.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>

static Buffer* getMaterialData(VKRT* vkrt) {
    return &vkrt->core.sceneMaterialData;
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

static void destroyTransfer(VKRT* vkrt, FrameTransfer* transfer) {
    if (!vkrt || !transfer) return;

    if (transfer->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkrt->core.device, transfer->buffer, NULL);
        transfer->buffer = VK_NULL_HANDLE;
    }
    if (transfer->memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkrt->core.device, transfer->memory, NULL);
        transfer->memory = VK_NULL_HANDLE;
    }
}

void cleanupFrameSceneUpdate(VKRT* vkrt, uint32_t frameIndex) {
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

    destroyTransfer(vkrt, &update->instanceBuffer);
    destroyTransfer(vkrt, &update->tlasScratch);

    update->tlasBuildPending = VK_FALSE;
}

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

VKRT_Result rebuildMaterialBuffer(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    Buffer* materialData = getMaterialData(vkrt);
    destroyBufferResources(vkrt, materialData);

    uint32_t materialCount = vkrt->core.meshCount;
    materialData->count = materialCount;
    if (materialCount == 0) {
        return rebuildLightBuffers(vkrt);
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

    if ((result = rebuildLightBuffers(vkrt)) != VKRT_SUCCESS) {
        return result;
    }

    return VKRT_SUCCESS;
}

VKRT_Result rebuildTopLevelScene(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    return createTopLevelAccelerationStructure(vkrt);
}
