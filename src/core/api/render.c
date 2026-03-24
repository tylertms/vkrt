#include "images.h"
#include "descriptor.h"
#include "scene.h"
#include "state.h"
#include "swapchain.h"
#include "view.h"
#include "export.h"
#include "vkrt_internal.h"
#include "numeric.h"

#include <math.h>
#include <string.h>

VKRT_Result VKRT_saveRenderImage(VKRT* vkrt, const char* path) {
    if (!vkrt || !path || !path[0]) return VKRT_ERROR_INVALID_ARGUMENT;
    return saveCurrentRenderImage(vkrt, path) == 0
        ? VKRT_SUCCESS
        : VKRT_ERROR_OPERATION_FAILED;
}

VKRT_Result VKRT_saveRenderPNG(VKRT* vkrt, const char* path) {
    return VKRT_saveRenderImage(vkrt, path);
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

    if (width == 0) width = 1u;
    if (height == 0) height = 1u;

    extent.width = width;
    extent.height = height;
    return extent;
}

static int vector3Finite(const vec3 value) {
    return value &&
        isfinite(value[0]) &&
        isfinite(value[1]) &&
        isfinite(value[2]);
}

static int cameraPoseValid(const Camera* camera) {
    if (!camera) return 0;
    if (!vector3Finite(camera->pos) || !vector3Finite(camera->target) || !vector3Finite(camera->up)) {
        return 0;
    }
    if (!isfinite(camera->vfov) || camera->vfov <= 0.0f || camera->vfov >= 179.0f) {
        return 0;
    }

    vec3 viewDirection = {
        camera->target[0] - camera->pos[0],
        camera->target[1] - camera->pos[1],
        camera->target[2] - camera->pos[2],
    };
    float viewLengthSquared = glm_vec3_norm2(viewDirection);
    vec3 upDirection = {
        camera->up[0],
        camera->up[1],
        camera->up[2],
    };
    float upLengthSquared = glm_vec3_norm2(upDirection);
    if (viewLengthSquared <= 1e-12f || upLengthSquared <= 1e-12f) {
        return 0;
    }

    vec3 viewCrossUp = {0.0f, 0.0f, 0.0f};
    glm_vec3_cross(viewDirection, upDirection, viewCrossUp);
    return glm_vec3_norm2(viewCrossUp) > 1e-12f;
}

static void applySceneViewport(VKRT* vkrt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!vkrt || !vkrt->core.sceneData) return;

    uint32_t* rect = vkrt->core.sceneData->viewportRect;
    if (rect[0] == x && rect[1] == y && rect[2] == width && rect[3] == height) {
        return;
    }

    rect[0] = x;
    rect[1] = y;
    rect[2] = width;
    rect[3] = height;
    updateCamera(vkrt);
}

static VKRT_Result recreateRenderTargets(VKRT* vkrt) {
    if (!vkrt || vkrt->core.device == VK_NULL_HANDLE) return VKRT_ERROR_INVALID_ARGUMENT;

    if (vkrtWaitForAllInFlightFrames(vkrt) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    GPUImageState previousState = {0};
    GPUImageState nextState = {0};
    captureGPUImageState(vkrt, &previousState);

    if (createGPUImageState(vkrt, vkrt->runtime.renderExtent, &nextState) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    applyGPUImageState(vkrt, &nextState);
    if (updateAllDescriptorSets(vkrt) != VKRT_SUCCESS) {
        applyGPUImageState(vkrt, &previousState);
        updateAllDescriptorSets(vkrt);
        destroyGPUImageState(vkrt, &nextState);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    destroyGPUImageState(vkrt, &previousState);
    return VKRT_SUCCESS;
}

static void resetRenderSessionState(VKRT* vkrt, VkBool32 resetViewTransform) {
    if (!vkrt) return;

    if (resetViewTransform) {
        vkrt->renderControl.view.zoom = 1.0f;
        vkrt->renderControl.view.panX = 0.0f;
        vkrt->renderControl.view.panY = 0.0f;
    }

    vkrt->renderControl.timing.lastFrameTimestamp = 0;
    vkrt->renderControl.autoSPP.controlMs = 0.0f;
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

    VkBool32 wasRenderModeActive = vkrt->renderStatus.renderModeActive != 0;
    VkBool32 usedRenderPresentProfile = vkrtUsesRenderPresentProfile(vkrt);
    VkBool32 extentChanged = vkrt->runtime.renderExtent.width != width || vkrt->runtime.renderExtent.height != height;
    VkExtent2D requestedExtent = {width, height};

    if (updateRenderExtent(vkrt, requestedExtent) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    vkrt->renderStatus.renderModeActive = 1;
    vkrt->renderStatus.renderTargetSamples = targetSamples;
    resetRenderSessionState(vkrt, !wasRenderModeActive || extentChanged);
    applySceneViewport(vkrt, 0, 0, width, height);
    resetSceneData(vkrt);
    vkrtRefreshPresentModeIfNeeded(vkrt, usedRenderPresentProfile);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_stopRenderSampling(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->renderStatus.renderModeActive || vkrt->renderStatus.renderModeFinished) {
        return VKRT_SUCCESS;
    }
    VkBool32 usedRenderPresentProfile = vkrtUsesRenderPresentProfile(vkrt);
    vkrt->renderStatus.renderModeFinished = 1;
    vkrtRefreshPresentModeIfNeeded(vkrt, usedRenderPresentProfile);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_stopRender(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrt->renderStatus.renderModeActive) return VKRT_SUCCESS;

    VkBool32 usedRenderPresentProfile = vkrtUsesRenderPresentProfile(vkrt);
    if (updateRenderExtent(vkrt, vkrt->runtime.swapChainExtent) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    vkrt->renderStatus.renderModeActive = 0;
    vkrt->renderStatus.renderTargetSamples = 0;
    resetRenderSessionState(vkrt, VK_TRUE);
    uint32_t x = vkrt->runtime.displayViewportRect[0];
    uint32_t y = vkrt->runtime.displayViewportRect[1];
    uint32_t width = vkrt->runtime.displayViewportRect[2];
    uint32_t height = vkrt->runtime.displayViewportRect[3];
    vkrtClampViewportRect(vkrt->runtime.swapChainExtent, &x, &y, &width, &height);
    applySceneViewport(vkrt, x, y, width, height);
    resetSceneData(vkrt);
    vkrtRefreshPresentModeIfNeeded(vkrt, usedRenderPresentProfile);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setRenderViewport(VKRT* vkrt, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    vkrtClampViewportRect(vkrt->runtime.swapChainExtent, &x, &y, &width, &height);
    if (vkrt->runtime.displayViewportRect[0] == x &&
        vkrt->runtime.displayViewportRect[1] == y &&
        vkrt->runtime.displayViewportRect[2] == width &&
        vkrt->runtime.displayViewportRect[3] == height) {
        if (vkrt->renderStatus.renderModeActive) return VKRT_SUCCESS;
        applySceneViewport(vkrt, x, y, width, height);
        return VKRT_SUCCESS;
    }

    vkrt->runtime.displayViewportRect[0] = x;
    vkrt->runtime.displayViewportRect[1] = y;
    vkrt->runtime.displayViewportRect[2] = width;
    vkrt->runtime.displayViewportRect[3] = height;

    if (vkrt->renderStatus.renderModeActive) return VKRT_SUCCESS;
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
    *outZoom = vkrt->renderControl.view.zoom;
    *outPanX = vkrt->renderControl.view.panX;
    *outPanY = vkrt->renderControl.view.panY;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setRenderViewState(VKRT* vkrt, float zoom, float panX, float panY) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    zoom = vkrtFiniteClampf(zoom, VKRT_RENDER_VIEW_ZOOM_MIN, VKRT_RENDER_VIEW_ZOOM_MIN, VKRT_RENDER_VIEW_ZOOM_MAX);
    panX = vkrtFiniteOrf(panX, 0.0f);
    panY = vkrtFiniteOrf(panY, 0.0f);
    VkExtent2D renderExtent = queryEffectiveRenderExtent(vkrt);
    VkExtent2D viewportExtent = queryEffectiveDisplayViewportExtent(vkrt);
    vkrtClampRenderViewPanOffset(renderExtent, viewportExtent, zoom, &panX, &panY);

    if (vkrt->renderControl.view.zoom == zoom &&
        vkrt->renderControl.view.panX == panX &&
        vkrt->renderControl.view.panY == panY) {
        return VKRT_SUCCESS;
    }

    vkrt->renderControl.view.zoom = zoom;
    vkrt->renderControl.view.panX = panX;
    vkrt->renderControl.view.panY = panY;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_cameraSetPose(VKRT* vkrt, vec3 position, vec3 target, vec3 up, float vfov) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    Camera nextCamera = vkrt->sceneSettings.camera;
    if (position) {
        if (!vector3Finite(position)) return VKRT_ERROR_INVALID_ARGUMENT;
        glm_vec3_copy(position, nextCamera.pos);
    }
    if (target) {
        if (!vector3Finite(target)) return VKRT_ERROR_INVALID_ARGUMENT;
        glm_vec3_copy(target, nextCamera.target);
    }
    if (up) {
        if (!vector3Finite(up)) return VKRT_ERROR_INVALID_ARGUMENT;
        glm_vec3_copy(up, nextCamera.up);
    }
    if (vfov != 0.0f) {
        if (!isfinite(vfov) || vfov <= 0.0f || vfov >= 179.0f) {
            return VKRT_ERROR_INVALID_ARGUMENT;
        }
        nextCamera.vfov = vfov;
    }

    if (!cameraPoseValid(&nextCamera)) return VKRT_ERROR_INVALID_ARGUMENT;

    if (memcmp(&vkrt->sceneSettings.camera, &nextCamera, sizeof(nextCamera)) == 0) {
        return VKRT_SUCCESS;
    }

    vkrt->sceneSettings.camera = nextCamera;

    updateCamera(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_cameraGetPose(const VKRT* vkrt, vec3 position, vec3 target, vec3 up, float* vfov) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (position) memcpy(position, vkrt->sceneSettings.camera.pos, sizeof(vec3));
    if (target) memcpy(target, vkrt->sceneSettings.camera.target, sizeof(vec3));
    if (up) memcpy(up, vkrt->sceneSettings.camera.up, sizeof(vec3));
    if (vfov) *vfov = vkrt->sceneSettings.camera.vfov;
    return VKRT_SUCCESS;
}
