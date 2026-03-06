#include "shared.h"

VKRT_Result vkrtRequireSceneStateReady(const VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->core.sceneData) return VKRT_ERROR_OPERATION_FAILED;
    return VKRT_SUCCESS;
}

VKRT_Result vkrtWaitForAllInFlightFrames(const VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkWaitForFences(vkrt->core.device,
            VKRT_MAX_FRAMES_IN_FLIGHT,
            vkrt->runtime.inFlightFences,
            VK_TRUE,
            UINT64_MAX) != VK_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
    return VKRT_SUCCESS;
}

static uint32_t vkrtMaterialRequiresBackfaces(const Material* material) {
    return (material && material->transmission > 0.0f) ? 1u : 0u;
}

uint32_t vkrtResolveMeshRenderBackfaces(const Mesh* mesh) {
    if (!mesh) return 0u;
    if (mesh->renderBackfacesOverride >= 0) {
        return mesh->renderBackfacesOverride ? 1u : 0u;
    }
    return vkrtMaterialRequiresBackfaces(&mesh->material);
}

void vkrtMarkSceneResourcesDirty(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.sceneRevision++;
}

void vkrtMarkMaterialResourcesDirty(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.materialRevision++;
}
