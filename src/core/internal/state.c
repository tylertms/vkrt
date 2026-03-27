#include "state.h"

#include "config.h"
#include "types.h"
#include "vkrt_engine_types.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static const char* kDefaultMaterialName = "Default Material";

VKRT_Result vkrtRequireSceneStateReady(const VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->core.sceneData) return VKRT_ERROR_OPERATION_FAILED;
    return VKRT_SUCCESS;
}

VKRT_Result vkrtWaitForAllInFlightFrames(const VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    VkResult result =
        vkWaitForFences(vkrt->core.device, VKRT_MAX_FRAMES_IN_FLIGHT, vkrt->runtime.inFlightFences, VK_TRUE, UINT64_MAX);
    return vkrtConvertVkResult(result);
}

VKRT_Result vkrtConvertVkResult(VkResult result) {
    switch (result) {
        case VK_SUCCESS:
        case VK_SUBOPTIMAL_KHR:
            return VKRT_SUCCESS;
        case VK_ERROR_DEVICE_LOST:
            return VKRT_ERROR_DEVICE_LOST;
        case VK_ERROR_OUT_OF_DATE_KHR:
            return VKRT_ERROR_SWAPCHAIN_OUT_OF_DATE;
        default:
            return VKRT_ERROR_OPERATION_FAILED;
    }
}

VKRT_Result vkrtEnsureDefaultMaterial(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrt->core.materialCount > 0) return VKRT_SUCCESS;

    SceneMaterial* materials = (SceneMaterial*)malloc(sizeof(SceneMaterial));
    if (!materials) return VKRT_ERROR_OUT_OF_MEMORY;

    materials[0].material = VKRT_materialDefault();
    snprintf(materials[0].name, sizeof(materials[0].name), "%s", kDefaultMaterialName);
    vkrt->core.materials = materials;
    vkrt->core.materialCount = 1u;
    return VKRT_SUCCESS;
}

const SceneMaterial* vkrtGetSceneMaterial(const VKRT* vkrt, uint32_t materialIndex) {
    if (!vkrt || materialIndex >= vkrt->core.materialCount || !vkrt->core.materials) return NULL;
    return &vkrt->core.materials[materialIndex];
}

const Material* vkrtGetSceneMaterialData(const VKRT* vkrt, uint32_t materialIndex) {
    const SceneMaterial* material = vkrtGetSceneMaterial(vkrt, materialIndex);
    return material ? &material->material : NULL;
}

uint32_t vkrtCountMaterialUsers(const VKRT* vkrt, uint32_t materialIndex) {
    if (!vkrt || materialIndex >= vkrt->core.materialCount) return 0u;

    uint32_t useCount = 0u;
    for (uint32_t meshIndex = 0; meshIndex < vkrt->core.meshCount; meshIndex++) {
        if (vkrt->core.meshes[meshIndex].info.materialIndex == materialIndex) {
            useCount++;
        }
    }
    return useCount;
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

    if (vkrt->core.procs.vkDestroyAccelerationStructureKHR && accelerationStructure->structure != VK_NULL_HANDLE) {
        vkrt->core.procs.vkDestroyAccelerationStructureKHR(vkrt->core.device, accelerationStructure->structure, NULL);
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

void vkrtMarkSelectionResourcesDirty(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.selectionRevision++;
}

void vkrtMarkMaterialResourcesDirty(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.materialRevision++;
}

void vkrtMarkTextureResourcesDirty(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.textureRevision++;
}

void vkrtMarkLightResourcesDirty(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.lightRevision++;
}
