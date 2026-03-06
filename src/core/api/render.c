#include "storage.h"
#include "shared.h"
#include "descriptor.h"
#include "scene.h"
#include "view.h"
#include "export.h"
#include "vkrt_internal.h"

#include <math.h>
#include <string.h>

VKRT_Result VKRT_saveRenderPNG(VKRT* vkrt, const char* path) {
    if (!vkrt || !path || !path[0]) return VKRT_ERROR_INVALID_ARGUMENT;
    return saveCurrentRenderPNG(vkrt, path) == 0
        ? VKRT_SUCCESS
        : VKRT_ERROR_OPERATION_FAILED;
}

static VkExtent2D queryEffectiveRenderExtent(const VKRT* vkrt) {
    if (!vkrt) return (VkExtent2D){1u, 1u};

    VkExtent2D extent = vkrt->runtime.renderExtent;
    if (extent.width == 0 || extent.height == 0) {
        extent = vkrt->runtime.swapChainExtent;
    }
    if (extent.width == 0) extent.width = 1u;
    if (extent.height == 0) extent.height = 1u;
    return extent;
}

static VkExtent2D queryEffectiveDisplayViewportExtent(const VKRT* vkrt) {
    if (!vkrt) return (VkExtent2D){1u, 1u};

    VkExtent2D extent = {
        .width = vkrt->runtime.displayViewportRect[2],
        .height = vkrt->runtime.displayViewportRect[3],
    };

    uint32_t width = extent.width;
    uint32_t height = extent.height;
    if (width == 0 || height == 0) {
        width = vkrt->runtime.swapChainExtent.width;
        height = vkrt->runtime.swapChainExtent.height;
    }
    if (width == 0) width = 1;
    if (height == 0) height = 1;

    extent.width = width;
    extent.height = height;
    return extent;
}

static void applySceneViewport(VKRT* vkrt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!vkrt || !vkrt->core.sceneData) return;
    uint32_t* rect = vkrt->core.sceneData->viewportRect;
    if (rect[0] == x && rect[1] == y && rect[2] == width && rect[3] == height &&
        vkrt->state.camera.width == width && vkrt->state.camera.height == height) {
        return;
    }

    rect[0] = x;
    rect[1] = y;
    rect[2] = width;
    rect[3] = height;

    vkrt->state.camera.width = width;
    vkrt->state.camera.height = height;
    updateMatricesFromCamera(vkrt);
}

static VKRT_Result recreateRenderTargets(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return VKRT_ERROR_INVALID_ARGUMENT;

    if (vkrtWaitForAllInFlightFrames(vkrt) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
    destroyStorageImage(vkrt);
    if (createStorageImage(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (updateAllDescriptorSets(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    return VKRT_SUCCESS;
}

static void resetRenderSessionState(VKRT* vkrt, VkBool32 resetViewTransform) {
    if (!vkrt) return;

    if (resetViewTransform) {
        vkrt->state.renderViewZoom = 1.0f;
        vkrt->state.renderViewPanX = 0.0f;
        vkrt->state.renderViewPanY = 0.0f;
    }

    vkrt->state.displayRenderTimeMs = 0.0f;
    vkrt->state.displayFrameTimeMs = 0.0f;
    vkrt->state.lastFrameTimestamp = 0;
    vkrt->state.autoSPPControlMs = 0.0f;
    vkrt->state.autoSPPFramesUntilNextAdjust = 0;
    vkrt->runtime.autoSPPFastAdaptFrames = vkrt->state.autoSPPEnabled ? 8u : 0u;
}

static VKRT_Result updateRenderExtent(VKRT* vkrt, VkExtent2D extent) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (vkrt->runtime.renderExtent.width == extent.width &&
        vkrt->runtime.renderExtent.height == extent.height) {
        return VKRT_SUCCESS;
    }

    VkExtent2D previousExtent = vkrt->runtime.renderExtent;
    vkrt->runtime.renderExtent = extent;
    if (recreateRenderTargets(vkrt) != VKRT_SUCCESS) {
        vkrt->runtime.renderExtent = previousExtent;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    return VKRT_SUCCESS;
}

VKRT_Result VKRT_startRender(VKRT* vkrt, uint32_t width, uint32_t height, uint32_t targetSamples) {
    if (!vkrt || width == 0 || height == 0) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->core.sceneData) return VKRT_ERROR_OPERATION_FAILED;

    if (width > 16384) width = 16384;
    if (height > 16384) height = 16384;

    VkBool32 wasRenderModeActive = vkrt->state.renderModeActive != 0;
    VkBool32 extentChanged = vkrt->runtime.renderExtent.width != width || vkrt->runtime.renderExtent.height != height;
    VkExtent2D requestedExtent = {width, height};
    uint8_t previousVsync = vkrt->runtime.vsync;
    VkBool32 previousFramebufferResized = vkrt->runtime.framebufferResized;

    if (!wasRenderModeActive) {
        vkrt->runtime.savedVsync = vkrt->runtime.vsync;
        if (vkrt->runtime.vsync) {
            vkrt->runtime.vsync = 0;
            vkrt->runtime.framebufferResized = VK_TRUE;
        }
    }
    if (vkrt->state.samplesPerPixel == 0) {
        vkrt->state.samplesPerPixel = 1;
        vkrt->core.sceneData->samplesPerPixel = 1;
    }

    if (updateRenderExtent(vkrt, requestedExtent) != VKRT_SUCCESS) {
        if (!wasRenderModeActive) {
            vkrt->runtime.vsync = previousVsync;
            vkrt->runtime.framebufferResized = previousFramebufferResized;
        }
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->state.renderModeActive = 1;
    vkrt->state.renderTargetSamples = targetSamples;
    resetRenderSessionState(vkrt, !wasRenderModeActive || extentChanged);
    applySceneViewport(vkrt, 0, 0, width, height);
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_stopRenderSampling(VKRT* vkrt) {
    if (!vkrt || !vkrt->state.renderModeActive) return VKRT_ERROR_INVALID_ARGUMENT;
    vkrt->state.renderModeFinished = 1;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setVSyncEnabled(VKRT* vkrt, uint8_t enabled) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint8_t normalized = enabled ? 1u : 0u;
    if (vkrt->state.renderModeActive) {
        vkrt->runtime.savedVsync = normalized;
        return VKRT_SUCCESS;
    }

    if (vkrt->runtime.vsync == normalized) return VKRT_SUCCESS;
    vkrt->runtime.vsync = normalized;
    vkrt->runtime.framebufferResized = VK_TRUE;
    return VKRT_SUCCESS;
}

static void restoreSavedVsync(VKRT* vkrt) {
    if (vkrt->runtime.vsync != vkrt->runtime.savedVsync) {
        vkrt->runtime.vsync = vkrt->runtime.savedVsync;
        vkrt->runtime.framebufferResized = VK_TRUE;
    }
}

VKRT_Result VKRT_stopRender(VKRT* vkrt) {
    if (!vkrt || !vkrt->state.renderModeActive) return VKRT_ERROR_INVALID_ARGUMENT;

    uint8_t previousVsync = vkrt->runtime.vsync;
    VkBool32 previousFramebufferResized = vkrt->runtime.framebufferResized;
    restoreSavedVsync(vkrt);
    if (updateRenderExtent(vkrt, vkrt->runtime.swapChainExtent) != VKRT_SUCCESS) {
        vkrt->runtime.vsync = previousVsync;
        vkrt->runtime.framebufferResized = previousFramebufferResized;
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->state.renderModeActive = 0;
    vkrt->state.renderTargetSamples = 0;
    resetRenderSessionState(vkrt, VK_TRUE);
    uint32_t x = vkrt->runtime.displayViewportRect[0];
    uint32_t y = vkrt->runtime.displayViewportRect[1];
    uint32_t width = vkrt->runtime.displayViewportRect[2];
    uint32_t height = vkrt->runtime.displayViewportRect[3];
    vkrtClampViewportRect(vkrt->runtime.swapChainExtent, &x, &y, &width, &height);
    applySceneViewport(vkrt, x, y, width, height);
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setRenderViewport(VKRT* vkrt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    vkrtClampViewportRect(vkrt->runtime.swapChainExtent, &x, &y, &width, &height);
    if (vkrt->runtime.displayViewportRect[0] == x &&
        vkrt->runtime.displayViewportRect[1] == y &&
        vkrt->runtime.displayViewportRect[2] == width &&
        vkrt->runtime.displayViewportRect[3] == height) {
        if (vkrt->state.renderModeActive) return VKRT_SUCCESS;
        applySceneViewport(vkrt, x, y, width, height);
        return VKRT_SUCCESS;
    }

    vkrt->runtime.displayViewportRect[0] = x;
    vkrt->runtime.displayViewportRect[1] = y;
    vkrt->runtime.displayViewportRect[2] = width;
    vkrt->runtime.displayViewportRect[3] = height;

    if (vkrt->state.renderModeActive) return VKRT_SUCCESS;
    applySceneViewport(vkrt, x, y, width, height);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getRenderSourceExtent(const VKRT* vkrt, float* outWidth, float* outHeight) {
    if (!vkrt || !outWidth || !outHeight) return VKRT_ERROR_INVALID_ARGUMENT;
    VkExtent2D extent = queryEffectiveRenderExtent(vkrt);
    *outWidth = (float)extent.width;
    *outHeight = (float)extent.height;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getDisplayViewportExtent(const VKRT* vkrt, float* outWidth, float* outHeight) {
    if (!vkrt || !outWidth || !outHeight) return VKRT_ERROR_INVALID_ARGUMENT;
    VkExtent2D extent = queryEffectiveDisplayViewportExtent(vkrt);
    *outWidth = (float)extent.width;
    *outHeight = (float)extent.height;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getRenderViewCrop(const VKRT* vkrt, float zoom, float* outWidth, float* outHeight) {
    if (!vkrt || !outWidth || !outHeight) return VKRT_ERROR_INVALID_ARGUMENT;
    VkExtent2D renderExtent = queryEffectiveRenderExtent(vkrt);
    VkExtent2D viewportExtent = queryEffectiveDisplayViewportExtent(vkrt);
    uint32_t cropWidth = 1u;
    uint32_t cropHeight = 1u;
    vkrtQueryRenderViewCropExtent(renderExtent, viewportExtent, zoom, &cropWidth, &cropHeight, NULL);
    *outWidth = (float)cropWidth;
    *outHeight = (float)cropHeight;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getRenderViewState(const VKRT* vkrt, float* outZoom, float* outPanX, float* outPanY) {
    if (!vkrt || !outZoom || !outPanX || !outPanY) return VKRT_ERROR_INVALID_ARGUMENT;
    *outZoom = vkrt->state.renderViewZoom;
    *outPanX = vkrt->state.renderViewPanX;
    *outPanY = vkrt->state.renderViewPanY;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setRenderViewState(VKRT* vkrt, float zoom, float panX, float panY) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (!isfinite(zoom)) zoom = VKRT_RENDER_VIEW_ZOOM_MIN;
    if (!isfinite(panX)) panX = 0.0f;
    if (!isfinite(panY)) panY = 0.0f;

    zoom = vkrtClampFloatValue(zoom, VKRT_RENDER_VIEW_ZOOM_MIN, VKRT_RENDER_VIEW_ZOOM_MAX);
    VkExtent2D renderExtent = queryEffectiveRenderExtent(vkrt);
    VkExtent2D viewportExtent = queryEffectiveDisplayViewportExtent(vkrt);
    vkrtClampRenderViewPanOffset(renderExtent, viewportExtent, zoom, &panX, &panY);

    if (vkrt->state.renderViewZoom == zoom &&
        vkrt->state.renderViewPanX == panX &&
        vkrt->state.renderViewPanY == panY) {
        return VKRT_SUCCESS;
    }

    vkrt->state.renderViewZoom = zoom;
    vkrt->state.renderViewPanX = panX;
    vkrt->state.renderViewPanY = panY;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_cameraSetPose(VKRT* vkrt, vec3 position, vec3 target, vec3 up, float vfov) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    if (position) glm_vec3_copy(position, vkrt->state.camera.pos);
    if (target) glm_vec3_copy(target, vkrt->state.camera.target);
    if (up) glm_vec3_copy(up, vkrt->state.camera.up);
    if (vfov > 0.0f) vkrt->state.camera.vfov = vfov;

    updateMatricesFromCamera(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_cameraGetPose(const VKRT* vkrt, vec3 position, vec3 target, vec3 up, float* vfov) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (position) memcpy(position, vkrt->state.camera.pos, sizeof(vec3));
    if (target) memcpy(target, vkrt->state.camera.target, sizeof(vec3));
    if (up) memcpy(up, vkrt->state.camera.up, sizeof(vec3));
    if (vfov) *vfov = vkrt->state.camera.vfov;
    return VKRT_SUCCESS;
}
