#include "images.h"
#include "descriptor.h"
#include "scene.h"
#include "state.h"
#include "view.h"
#include "export.h"
#include "vkrt_internal.h"
#include "numeric.h"

#include <string.h>

static int presentModePreferenceUsesSync(VKRT_PresentModePreference preference) {
    return preference != VKRT_PRESENT_MODE_IMMEDIATE;
}

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

    if (width == 0) width = 1u;
    if (height == 0) height = 1u;

    extent.width = width;
    extent.height = height;
    return extent;
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
    destroyGPUImages(vkrt);
    if (createGPUImages(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    if (updateAllDescriptorSets(vkrt) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;
    return VKRT_SUCCESS;
}

static void resetRenderSessionState(VKRT* vkrt, VkBool32 resetViewTransform) {
    if (!vkrt) return;

    if (resetViewTransform) {
        vkrt->renderView.zoom = 1.0f;
        vkrt->renderView.panX = 0.0f;
        vkrt->renderView.panY = 0.0f;
    }

    vkrt->renderStatus.displayRenderTimeMs = 0.0f;
    vkrt->renderStatus.displayFrameTimeMs = 0.0f;
    vkrt->timing.lastFrameTimestamp = 0;
    vkrt->autoSPP.controlMs = 0.0f;
    vkrt->autoSPP.framesUntilNextAdjust = 0;
    vkrt->runtime.autoSPPFastAdaptFrames = vkrt->sceneSettings.autoSPPEnabled ? 8u : 0u;
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
    VkBool32 extentChanged = vkrt->runtime.renderExtent.width != width || vkrt->runtime.renderExtent.height != height;
    VkExtent2D requestedExtent = {width, height};
    VKRT_PresentModePreference previousPresentModePreference = vkrt->runtime.presentModePreference;
    VkBool32 previousFramebufferResized = vkrt->runtime.framebufferResized;

    if (!wasRenderModeActive) {
        vkrt->runtime.savedPresentModePreference = vkrt->runtime.presentModePreference;
        if (presentModePreferenceUsesSync(vkrt->runtime.presentModePreference)) {
            vkrt->runtime.presentModePreference = VKRT_PRESENT_MODE_IMMEDIATE;
            vkrt->runtime.framebufferResized = VK_TRUE;
        }
    }

    if (vkrt->sceneSettings.samplesPerPixel == 0) {
        vkrt->sceneSettings.samplesPerPixel = 1;
    }

    if (updateRenderExtent(vkrt, requestedExtent) != VKRT_SUCCESS) {
        if (!wasRenderModeActive) {
            vkrt->runtime.presentModePreference = previousPresentModePreference;
            vkrt->runtime.framebufferResized = previousFramebufferResized;
        }
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->renderStatus.renderModeActive = 1;
    vkrt->renderStatus.renderTargetSamples = targetSamples;
    resetRenderSessionState(vkrt, !wasRenderModeActive || extentChanged);
    applySceneViewport(vkrt, 0, 0, width, height);
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_stopRenderSampling(VKRT* vkrt) {
    if (!vkrt || !vkrt->renderStatus.renderModeActive) return VKRT_ERROR_INVALID_ARGUMENT;
    vkrt->renderStatus.renderModeFinished = 1;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setVSyncEnabled(VKRT* vkrt, uint8_t enabled) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT_PresentModePreference preference = enabled
        ? VKRT_PRESENT_MODE_VSYNC
        : VKRT_PRESENT_MODE_IMMEDIATE;

    if (vkrt->renderStatus.renderModeActive) {
        vkrt->runtime.savedPresentModePreference = preference;
        return VKRT_SUCCESS;
    }

    if (vkrt->runtime.presentModePreference == preference) return VKRT_SUCCESS;
    vkrt->runtime.presentModePreference = preference;
    vkrt->runtime.framebufferResized = VK_TRUE;
    return VKRT_SUCCESS;
}

static void restoreSavedPresentModePreference(VKRT* vkrt) {
    if (vkrt->runtime.presentModePreference != vkrt->runtime.savedPresentModePreference) {
        vkrt->runtime.presentModePreference = vkrt->runtime.savedPresentModePreference;
        vkrt->runtime.framebufferResized = VK_TRUE;
    }
}

VKRT_Result VKRT_stopRender(VKRT* vkrt) {
    if (!vkrt || !vkrt->renderStatus.renderModeActive) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT_PresentModePreference previousPresentModePreference = vkrt->runtime.presentModePreference;
    VkBool32 previousFramebufferResized = vkrt->runtime.framebufferResized;
    restoreSavedPresentModePreference(vkrt);
    if (updateRenderExtent(vkrt, vkrt->runtime.swapChainExtent) != VKRT_SUCCESS) {
        vkrt->runtime.presentModePreference = previousPresentModePreference;
        vkrt->runtime.framebufferResized = previousFramebufferResized;
        return VKRT_ERROR_OPERATION_FAILED;
    }

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
    *outZoom = vkrt->renderView.zoom;
    *outPanX = vkrt->renderView.panX;
    *outPanY = vkrt->renderView.panY;
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

    if (vkrt->renderView.zoom == zoom &&
        vkrt->renderView.panX == panX &&
        vkrt->renderView.panY == panY) {
        return VKRT_SUCCESS;
    }

    vkrt->renderView.zoom = zoom;
    vkrt->renderView.panX = panX;
    vkrt->renderView.panY = panY;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_cameraSetPose(VKRT* vkrt, vec3 position, vec3 target, vec3 up, float vfov) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    if (position) glm_vec3_copy(position, vkrt->sceneSettings.camera.pos);
    if (target) glm_vec3_copy(target, vkrt->sceneSettings.camera.target);
    if (up) glm_vec3_copy(up, vkrt->sceneSettings.camera.up);
    if (vfov > 0.0f) vkrt->sceneSettings.camera.vfov = vfov;

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
