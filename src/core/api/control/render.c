#include "image/storage_image.h"
#include "descriptor.h"
#include "scene.h"
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

static float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static void clampViewportToSwapchain(const VKRT* vkrt, uint32_t* x, uint32_t* y, uint32_t* width, uint32_t* height) {
    if (!vkrt || !x || !y || !width || !height) return;
    uint32_t fullWidth = vkrt->runtime.swapChainExtent.width;
    uint32_t fullHeight = vkrt->runtime.swapChainExtent.height;
    if (fullWidth == 0 || fullHeight == 0) {
        *x = 0;
        *y = 0;
        *width = 0;
        *height = 0;
        return;
    }

    if (*width == 0 || *height == 0) {
        *x = 0;
        *y = 0;
        *width = fullWidth;
        *height = fullHeight;
    }

    if (*width <= 1 || *height <= 1) {
        *x = 0;
        *y = 0;
        *width = fullWidth;
        *height = fullHeight;
    }

    if (*x >= fullWidth) *x = fullWidth - 1;
    if (*y >= fullHeight) *y = fullHeight - 1;
    if (*x + *width > fullWidth) *width = fullWidth - *x;
    if (*y + *height > fullHeight) *height = fullHeight - *y;
}

static void queryRenderSourceExtentInternal(const VKRT* vkrt, float* outWidth, float* outHeight) {
    if (!vkrt || !outWidth || !outHeight) return;

    uint32_t width = vkrt->runtime.renderExtent.width;
    uint32_t height = vkrt->runtime.renderExtent.height;
    if (width == 0 || height == 0) {
        width = vkrt->runtime.swapChainExtent.width;
        height = vkrt->runtime.swapChainExtent.height;
    }
    if (width == 0) width = 1;
    if (height == 0) height = 1;

    *outWidth = (float)width;
    *outHeight = (float)height;
}

static void queryDisplayViewportExtentInternal(const VKRT* vkrt, float* outWidth, float* outHeight) {
    if (!vkrt || !outWidth || !outHeight) return;

    uint32_t width = vkrt->runtime.displayViewportRect[2];
    uint32_t height = vkrt->runtime.displayViewportRect[3];
    if (width == 0 || height == 0) {
        width = vkrt->runtime.swapChainExtent.width;
        height = vkrt->runtime.swapChainExtent.height;
    }
    if (width == 0) width = 1;
    if (height == 0) height = 1;

    *outWidth = (float)width;
    *outHeight = (float)height;
}

static void queryRenderViewCropInternal(const VKRT* vkrt, float zoom, float* outWidth, float* outHeight) {
    if (!vkrt || !outWidth || !outHeight) return;

    float sourceWidth = 1.0f;
    float sourceHeight = 1.0f;
    queryRenderSourceExtentInternal(vkrt, &sourceWidth, &sourceHeight);

    float clampedZoom = clampFloat(zoom, VKRT_RENDER_VIEW_ZOOM_MIN, VKRT_RENDER_VIEW_ZOOM_MAX);
    VkBool32 fillViewport = clampedZoom > (VKRT_RENDER_VIEW_ZOOM_MIN + 0.0001f);
    float cropWidth = sourceWidth;
    float cropHeight = sourceHeight;

    if (fillViewport) {
        float viewWidth = 1.0f;
        float viewHeight = 1.0f;
        queryDisplayViewportExtentInternal(vkrt, &viewWidth, &viewHeight);

        float sourceAspect = sourceWidth / sourceHeight;
        float viewAspect = viewWidth / viewHeight;
        float baseWidth = sourceWidth;
        float baseHeight = sourceHeight;
        if (viewAspect > sourceAspect) {
            baseHeight = sourceWidth / viewAspect;
        } else {
            baseWidth = sourceHeight * viewAspect;
        }

        cropWidth = baseWidth / clampedZoom;
        cropHeight = baseHeight / clampedZoom;
        if (cropWidth < 1.0f) cropWidth = 1.0f;
        if (cropHeight < 1.0f) cropHeight = 1.0f;
        if (cropWidth > sourceWidth) cropWidth = sourceWidth;
        if (cropHeight > sourceHeight) cropHeight = sourceHeight;
    }

    *outWidth = cropWidth;
    *outHeight = cropHeight;
}

static void clampRenderViewPanInternal(const VKRT* vkrt, float zoom, float* panX, float* panY) {
    if (!vkrt || !panX || !panY) return;

    float sourceWidth = 1.0f;
    float sourceHeight = 1.0f;
    queryRenderSourceExtentInternal(vkrt, &sourceWidth, &sourceHeight);

    float cropWidth = sourceWidth;
    float cropHeight = sourceHeight;
    queryRenderViewCropInternal(vkrt, zoom, &cropWidth, &cropHeight);

    float maxPanX = (sourceWidth - cropWidth) * 0.5f;
    float maxPanY = (sourceHeight - cropHeight) * 0.5f;

    if (maxPanX <= 0.0f) *panX = 0.0f;
    else *panX = clampFloat(*panX, -maxPanX, maxPanX);

    if (maxPanY <= 0.0f) *panY = 0.0f;
    else *panY = clampFloat(*panY, -maxPanY, maxPanY);
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

    vkDeviceWaitIdle(vkrt->core.device);
    destroyStorageImage(vkrt);
    if (createStorageImage(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (updateDescriptorSet(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_startRender(VKRT* vkrt, uint32_t width, uint32_t height, uint32_t targetSamples) {
    if (!vkrt || width == 0 || height == 0) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->core.sceneData) return VKRT_ERROR_OPERATION_FAILED;

    if (width > 16384) width = 16384;
    if (height > 16384) height = 16384;

    VkBool32 wasRenderModeActive = vkrt->state.renderModeActive != 0;
    VkBool32 extentChanged = vkrt->runtime.renderExtent.width != width || vkrt->runtime.renderExtent.height != height;

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

    vkrt->runtime.renderExtent = (VkExtent2D){width, height};
    if (recreateRenderTargets(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    vkrt->state.renderModeActive = 1;
    vkrt->state.renderModeFinished = 0;
    vkrt->state.renderTargetSamples = targetSamples;

    if (!wasRenderModeActive || extentChanged) {
        vkrt->state.renderViewZoom = 1.0f;
        vkrt->state.renderViewPanX = 0.0f;
        vkrt->state.renderViewPanY = 0.0f;
        vkrt->state.displayRenderTimeMs = 0.0f;
        vkrt->state.displayFrameTimeMs = 0.0f;
        vkrt->state.lastFrameTimestamp = 0;
        vkrt->state.autoSPPControlMs = 0.0f;
        vkrt->state.autoSPPFramesUntilNextAdjust = 0;
        vkrt->runtime.autoSPPFastAdaptFrames = vkrt->state.autoSPPEnabled ? 8 : 0;
    } else if (!vkrt->state.autoSPPEnabled) {
        vkrt->state.autoSPPControlMs = 0.0f;
        vkrt->state.autoSPPFramesUntilNextAdjust = 0;
        vkrt->runtime.autoSPPFastAdaptFrames = 0;
    }

    applySceneViewport(vkrt, 0, 0, width, height);
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

    restoreSavedVsync(vkrt);
    vkrt->state.renderModeActive = 0;
    vkrt->state.renderModeFinished = 0;
    vkrt->state.renderTargetSamples = 0;
    vkrt->state.renderViewZoom = 1.0f;
    vkrt->state.renderViewPanX = 0.0f;
    vkrt->state.renderViewPanY = 0.0f;

    vkrt->state.autoSPPControlMs = 0.0f;
    vkrt->state.autoSPPFramesUntilNextAdjust = 0;
    vkrt->runtime.autoSPPFastAdaptFrames = vkrt->state.autoSPPEnabled ? 8 : 0;

    vkrt->runtime.renderExtent = vkrt->runtime.swapChainExtent;
    if (recreateRenderTargets(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    uint32_t x = vkrt->runtime.displayViewportRect[0];
    uint32_t y = vkrt->runtime.displayViewportRect[1];
    uint32_t width = vkrt->runtime.displayViewportRect[2];
    uint32_t height = vkrt->runtime.displayViewportRect[3];
    clampViewportToSwapchain(vkrt, &x, &y, &width, &height);
    applySceneViewport(vkrt, x, y, width, height);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setRenderViewport(VKRT* vkrt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    clampViewportToSwapchain(vkrt, &x, &y, &width, &height);
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
    queryRenderSourceExtentInternal(vkrt, outWidth, outHeight);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getDisplayViewportExtent(const VKRT* vkrt, float* outWidth, float* outHeight) {
    if (!vkrt || !outWidth || !outHeight) return VKRT_ERROR_INVALID_ARGUMENT;
    queryDisplayViewportExtentInternal(vkrt, outWidth, outHeight);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getRenderViewCrop(const VKRT* vkrt, float zoom, float* outWidth, float* outHeight) {
    if (!vkrt || !outWidth || !outHeight) return VKRT_ERROR_INVALID_ARGUMENT;
    queryRenderViewCropInternal(vkrt, zoom, outWidth, outHeight);
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

    zoom = clampFloat(zoom, VKRT_RENDER_VIEW_ZOOM_MIN, VKRT_RENDER_VIEW_ZOOM_MAX);
    clampRenderViewPanInternal(vkrt, zoom, &panX, &panY);

    vkrt->state.renderViewZoom = zoom;
    vkrt->state.renderViewPanX = panX;
    vkrt->state.renderViewPanY = panY;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_cameraSetPose(VKRT* vkrt, vec3 position, vec3 target, vec3 up, float vfov) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

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
