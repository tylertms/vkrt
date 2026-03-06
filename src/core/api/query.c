#include "vkrt_internal.h"

#include <stdio.h>
#include <string.h>

VKRT_Result VKRT_getPublicState(const VKRT* vkrt, VKRT_PublicState* outState) {
    if (!vkrt || !outState) return VKRT_ERROR_INVALID_ARGUMENT;
    *outState = vkrt->state;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getRuntimeSnapshot(const VKRT* vkrt, VKRT_RuntimeSnapshot* outRuntime) {
    if (!vkrt || !outRuntime) return VKRT_ERROR_INVALID_ARGUMENT;

    outRuntime->swapchainWidth = vkrt->runtime.swapChainExtent.width;
    outRuntime->swapchainHeight = vkrt->runtime.swapChainExtent.height;
    outRuntime->renderWidth = vkrt->runtime.renderExtent.width;
    outRuntime->renderHeight = vkrt->runtime.renderExtent.height;
    memcpy(outRuntime->displayViewportRect, vkrt->runtime.displayViewportRect, sizeof(outRuntime->displayViewportRect));
    outRuntime->vsync = vkrt->runtime.vsync;
    outRuntime->savedVsync = vkrt->runtime.savedVsync;
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
    outOverlayInfo->graphicsQueueFamily =
        vkrt->core.indices.graphics >= 0 ? (uint32_t)vkrt->core.indices.graphics : 0u;
    outOverlayInfo->graphicsQueue = vkrt->core.graphicsQueue;
    outOverlayInfo->descriptorPool = vkrt->core.descriptorPool;
    outOverlayInfo->renderPass = vkrt->runtime.renderPass;
    outOverlayInfo->swapchainImageCount = (uint32_t)vkrt->runtime.swapChainImageCount;
    outOverlayInfo->swapchainMinImageCount =
        outOverlayInfo->swapchainImageCount > 1u
            ? outOverlayInfo->swapchainImageCount - 1u
            : outOverlayInfo->swapchainImageCount;
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
    return VKRT_SUCCESS;
}
