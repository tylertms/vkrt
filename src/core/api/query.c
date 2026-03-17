#include "vkrt_internal.h"

#include <stdio.h>
#include <string.h>

VKRT_Result VKRT_getSceneSettings(const VKRT* vkrt, VKRT_SceneSettingsSnapshot* outSettings) {
    if (!vkrt || !outSettings) return VKRT_ERROR_INVALID_ARGUMENT;
    *outSettings = vkrt->sceneSettings;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getRenderStatus(const VKRT* vkrt, VKRT_RenderStatusSnapshot* outStatus) {
    if (!vkrt || !outStatus) return VKRT_ERROR_INVALID_ARGUMENT;
    *outStatus = vkrt->renderStatus;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getRuntimeSnapshot(const VKRT* vkrt, VKRT_RuntimeSnapshot* outRuntime) {
    if (!vkrt || !outRuntime) return VKRT_ERROR_INVALID_ARGUMENT;

    outRuntime->displayWidth = vkrt->runtime.displayWidth;
    outRuntime->displayHeight = vkrt->runtime.displayHeight;
    outRuntime->swapchainWidth = vkrt->runtime.swapChainExtent.width;
    outRuntime->swapchainHeight = vkrt->runtime.swapChainExtent.height;
    outRuntime->renderWidth = vkrt->runtime.renderExtent.width;
    outRuntime->renderHeight = vkrt->runtime.renderExtent.height;
    memcpy(outRuntime->displayViewportRect, vkrt->runtime.displayViewportRect, sizeof(outRuntime->displayViewportRect));
    outRuntime->presentMode = vkrt->runtime.presentMode;
    outRuntime->displayRefreshHz = vkrt->runtime.displayRefreshHz;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getSystemInfo(const VKRT* vkrt, VKRT_SystemInfo* outSystemInfo) {
    if (!vkrt || !outSystemInfo) return VKRT_ERROR_INVALID_ARGUMENT;

    memset(outSystemInfo, 0, sizeof(*outSystemInfo));
    snprintf(outSystemInfo->deviceName, sizeof(outSystemInfo->deviceName), "%s", vkrt->core.deviceName);
    outSystemInfo->vendorID = vkrt->core.vendorID;
    outSystemInfo->driverVersion = vkrt->core.driverVersion;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getOverlayInfo(const VKRT* vkrt, VKRT_OverlayInfo* outOverlayInfo) {
    if (!vkrt || !outOverlayInfo) return VKRT_ERROR_INVALID_ARGUMENT;

    memset(outOverlayInfo, 0, sizeof(*outOverlayInfo));
    outOverlayInfo->window = vkrt->runtime.window;
    outOverlayInfo->instance = vkrt->core.instance;
    outOverlayInfo->physicalDevice = vkrt->core.physicalDevice;
    outOverlayInfo->device = vkrt->core.device;
    outOverlayInfo->graphicsQueueFamily = vkrt->core.indices.graphics >= 0
        ? (uint32_t)vkrt->core.indices.graphics
        : 0u;
    outOverlayInfo->graphicsQueue = vkrt->core.graphicsQueue;
    outOverlayInfo->descriptorPool = vkrt->core.overlayDescriptorPool;
    outOverlayInfo->renderPass = vkrt->runtime.renderPass;
    outOverlayInfo->swapchainImageCount = (uint32_t)vkrt->runtime.swapChainImageCount;
    outOverlayInfo->swapchainMinImageCount = vkrt->runtime.swapChainMinImageCount;

    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getMeshSnapshot(const VKRT* vkrt, uint32_t meshIndex, VKRT_MeshSnapshot* outMesh) {
    if (!vkrt || !outMesh) return VKRT_ERROR_INVALID_ARGUMENT;
    if (meshIndex >= vkrt->core.meshCount) return VKRT_ERROR_INVALID_ARGUMENT;

    const Mesh* mesh = &vkrt->core.meshes[meshIndex];
    outMesh->info = mesh->info;
    outMesh->material = mesh->material;
    outMesh->geometrySource = mesh->geometrySource;
    outMesh->ownsGeometry = mesh->ownsGeometry;
    snprintf(outMesh->name, sizeof(outMesh->name), "%s", mesh->name);
    return VKRT_SUCCESS;
}
