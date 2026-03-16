#include "state.h"

#include <stdint.h>

VKRT_Result vkrtRequireSceneStateReady(const VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->core.sceneData) return VKRT_ERROR_OPERATION_FAILED;
    return VKRT_SUCCESS;
}

VKRT_Result vkrtWaitForAllInFlightFrames(const VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkWaitForFences(
        vkrt->core.device,
        VKRT_MAX_FRAMES_IN_FLIGHT,
        vkrt->runtime.inFlightFences,
        VK_TRUE,
        UINT64_MAX
    ) != VK_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
    return VKRT_SUCCESS;
}

uint32_t vkrtResolveMeshRenderBackfaces(const Mesh* mesh) {
    if (!mesh) return 0u;
    if (mesh->renderBackfacesOverride >= 0) {
        return mesh->renderBackfacesOverride ? 1u : 0u;
    }
    return mesh->info.renderBackfaces ? 1u : 0u;
}

void vkrtDestroyAccelerationStructureResources(VKRT* vkrt, AccelerationStructure* accelerationStructure) {
    if (!vkrt || !accelerationStructure || vkrt->core.device == VK_NULL_HANDLE) return;

    if (vkrt->core.procs.vkDestroyAccelerationStructureKHR &&
        accelerationStructure->structure != VK_NULL_HANDLE) {
        vkrt->core.procs.vkDestroyAccelerationStructureKHR(
            vkrt->core.device,
            accelerationStructure->structure,
            NULL
        );
    }
    accelerationStructure->structure = VK_NULL_HANDLE;

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

void vkrtMarkSceneResourcesDirty(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.sceneRevision++;
}

void vkrtMarkMaterialResourcesDirty(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.materialRevision++;
}

void vkrtMarkLightResourcesDirty(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.lightRevision++;
}
