#include "config.h"
#include "constants.h"
#include "numeric.h"
#include "scene.h"
#include "state.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <types.h>
#include <vulkan/vulkan_core.h>

static uint32_t sanitizeMeshSelection(const VKRT* vkrt, uint32_t meshIndex) {
    if (!vkrt || meshIndex == VKRT_INVALID_INDEX) return VKRT_INVALID_INDEX;
    return meshIndex < vkrt->core.meshCount ? meshIndex : VKRT_INVALID_INDEX;
}

static void cancelPendingSelection(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.selectionPending = 0;
    vkrt->core.selectionSubmitted = 0;
    vkrt->core.selectionResultReady = 0;
    vkrt->core.selectionResultMeshIndex = VKRT_INVALID_INDEX;
    if (vkrt->core.selectionData) {
        vkrt->core.selectionData->hitMeshIndex = VKRT_INVALID_INDEX;
    }
}

static uint32_t sanitizeAutoSPPTargetFPS(const VKRT* vkrt, uint32_t targetFPS) {
    if (targetFPS == 0) {
        float refreshHz = vkrt ? vkrt->runtime.displayRefreshHz : 0.0f;
        if (refreshHz <= 0.0f) refreshHz = 60.0f;
        targetFPS = (uint32_t)(refreshHz + 0.5f);
    }

    if (targetFPS < 30u) return 30u;
    if (targetFPS > 360u) return 360u;
    return targetFPS;
}

VKRT_Result VKRT_applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input) {
    if (!input) return VKRT_ERROR_INVALID_ARGUMENT;
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    applyCameraInput(vkrt, input);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_invalidateAccumulation(VKRT* vkrt) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setSamplesPerPixel(VKRT* vkrt, uint32_t samplesPerPixel) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (samplesPerPixel == 0) samplesPerPixel = 1;

    if (vkrt->sceneSettings.samplesPerPixel == samplesPerPixel) return VKRT_SUCCESS;
    vkrt->sceneSettings.samplesPerPixel = samplesPerPixel;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setPathDepth(VKRT* vkrt, uint32_t rrMinDepth, uint32_t rrMaxDepth) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    if (rrMaxDepth < 1u) rrMaxDepth = 1u;
    if (rrMaxDepth > 64u) rrMaxDepth = 64u;
    if (rrMinDepth > rrMaxDepth) rrMinDepth = rrMaxDepth;

    if (vkrt->sceneSettings.rrMinDepth == rrMinDepth && vkrt->sceneSettings.rrMaxDepth == rrMaxDepth) {
        return VKRT_SUCCESS;
    }

    vkrt->sceneSettings.rrMinDepth = rrMinDepth;
    vkrt->sceneSettings.rrMaxDepth = rrMaxDepth;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setAutoSPPEnabled(VKRT* vkrt, uint8_t enabled) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    vkrt->sceneSettings.autoSPPEnabled = enabled ? 1 : 0;
    resetAutoSPPState(vkrt, VK_FALSE);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setAutoSPPTargetFPS(VKRT* vkrt, uint32_t targetFPS) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    targetFPS = sanitizeAutoSPPTargetFPS(vkrt, targetFPS);
    vkrt->sceneSettings.autoSPPTargetFPS = targetFPS;
    vkrt->renderControl.autoSPP.targetFrameMs = 1000.0f / (float)targetFPS;
    resetAutoSPPState(vkrt, VK_FALSE);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setToneMappingMode(VKRT* vkrt, VKRT_ToneMappingMode toneMappingMode) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    if ((uint32_t)toneMappingMode >= VKRT_TONE_MAPPING_MODE_COUNT) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    if (vkrt->sceneSettings.toneMappingMode == toneMappingMode) return VKRT_SUCCESS;
    vkrt->sceneSettings.toneMappingMode = toneMappingMode;
    syncSceneStateData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setRenderMode(VKRT* vkrt, VKRT_RenderMode renderMode) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    if ((uint32_t)renderMode >= VKRT_RENDER_MODE_COUNT) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    if (vkrt->sceneSettings.renderMode == renderMode) return VKRT_SUCCESS;
    vkrt->sceneSettings.renderMode = renderMode;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setExposure(VKRT* vkrt, float exposure) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    exposure = vkrtFiniteClampf(exposure, 1.0f, 0.0f, INFINITY);
    if (vkrt->sceneSettings.exposure == exposure) return VKRT_SUCCESS;

    vkrt->sceneSettings.exposure = exposure;
    syncSceneStateData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setAutoExposureEnabled(VKRT* vkrt, uint8_t enabled) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    enabled = enabled ? 1u : 0u;
    if (vkrt->sceneSettings.autoExposureEnabled == enabled) return VKRT_SUCCESS;

    vkrt->sceneSettings.autoExposureEnabled = enabled;
    vkrt->renderControl.autoExposure.filteredLuminance = 0.0f;
    for (uint32_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        vkrt->renderControl.autoExposure.readbacks[i].pending = 0u;
    }
    syncSceneStateData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setEnvironmentLight(VKRT* vkrt, vec3 color, float strength) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (!color) return VKRT_ERROR_INVALID_ARGUMENT;

    vec3 sanitizedColor;
    for (int channel = 0; channel < 3; channel++) {
        sanitizedColor[channel] = vkrtFiniteClampf(color[channel], 1.0f, 0.0f, INFINITY);
    }
    strength = vkrtFiniteClampf(strength, 0.0f, 0.0f, INFINITY);

    if (vkrt->sceneSettings.environmentColor[0] == sanitizedColor[0] &&
        vkrt->sceneSettings.environmentColor[1] == sanitizedColor[1] &&
        vkrt->sceneSettings.environmentColor[2] == sanitizedColor[2] &&
        vkrt->sceneSettings.environmentStrength == strength) {
        return VKRT_SUCCESS;
    }

    memcpy(vkrt->sceneSettings.environmentColor, sanitizedColor, sizeof(sanitizedColor));
    vkrt->sceneSettings.environmentStrength = strength;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setEnvironmentRotation(VKRT* vkrt, float rotationDegrees) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    rotationDegrees = vkrtFiniteOrf(rotationDegrees, 0.0f);
    rotationDegrees = fmodf(rotationDegrees, 360.0f);
    if (rotationDegrees < -180.0f) {
        rotationDegrees += 360.0f;
    }
    if (rotationDegrees >= 180.0f) {
        rotationDegrees -= 360.0f;
    }

    if (vkrt->sceneSettings.environmentRotation == rotationDegrees) return VKRT_SUCCESS;

    vkrt->sceneSettings.environmentRotation = rotationDegrees;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setDebugMode(VKRT* vkrt, uint32_t mode) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (mode >= VKRT_DEBUG_MODE_COUNT) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrt->sceneSettings.debugMode == mode) return VKRT_SUCCESS;
    vkrt->sceneSettings.debugMode = mode;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setMISNEEEnabled(VKRT* vkrt, uint32_t enabled) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    enabled = enabled ? 1u : 0u;
    if (vkrt->sceneSettings.misNeeEnabled == enabled) return VKRT_SUCCESS;
    vkrt->sceneSettings.misNeeEnabled = enabled;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setTimeRange(VKRT* vkrt, float timeBase, float timeStep) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    timeBase = vkrtFiniteOrf(timeBase, -1.0f);
    timeStep = vkrtFiniteOrf(timeStep, timeBase);
    if (timeBase < 0.0f) {
        timeBase = -1.0f;
        timeStep = -1.0f;
    } else if (timeStep < timeBase) {
        timeStep = timeBase;
    }

    if (vkrt->sceneSettings.timeBase == timeBase && vkrt->sceneSettings.timeStep == timeStep) return VKRT_SUCCESS;

    vkrt->sceneSettings.timeBase = timeBase;
    vkrt->sceneSettings.timeStep = timeStep;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_requestSelectionAtPixel(VKRT* vkrt, uint32_t x, uint32_t y) {
    if (!vkrt || !vkrt->core.selectionData) return VKRT_ERROR_INVALID_ARGUMENT;
    if (x > 0xFFFFu || y > 0xFFFFu) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrt->core.selectionPending) return VKRT_SUCCESS;

    Selection* selectionData = vkrt->core.selectionData;
    selectionData->pixel = (x & 0xFFFFu) | ((y & 0xFFFFu) << 16);
    selectionData->hitMeshIndex = VKRT_INVALID_INDEX;
    vkrt->core.selectionPendingFrame = vkrt->runtime.currentFrame;
    vkrt->core.selectionPending = 1;
    vkrt->core.selectionSubmitted = 0;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_consumeSelectedMesh(VKRT* vkrt, uint32_t* outMeshIndex, uint8_t* outReady) {
    if (!vkrt || !outMeshIndex || !outReady) return VKRT_ERROR_INVALID_ARGUMENT;

    *outMeshIndex = VKRT_INVALID_INDEX;
    *outReady = 0;
    if (!vkrt->core.selectionResultReady) return VKRT_SUCCESS;

    *outMeshIndex = vkrt->core.selectionResultMeshIndex;
    *outReady = 1;
    vkrt->core.selectionResultReady = 0;
    vkrt->core.selectionResultMeshIndex = VKRT_INVALID_INDEX;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setSelectedMesh(VKRT* vkrt, uint32_t meshIndex) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    uint32_t nextSelectedMesh = sanitizeMeshSelection(vkrt, meshIndex);
    cancelPendingSelection(vkrt);
    if (vkrt->sceneSettings.selectedMeshIndex == nextSelectedMesh) return VKRT_SUCCESS;

    vkrt->sceneSettings.selectedMeshIndex = nextSelectedMesh;
    vkrt->sceneSettings.selectionEnabled = nextSelectedMesh != VKRT_INVALID_INDEX;

    vkrtMarkSelectionResourcesDirty(vkrt);
    syncSelectionSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getSelectedMesh(const VKRT* vkrt, uint32_t* outMeshIndex) {
    if (!vkrt || !outMeshIndex) return VKRT_ERROR_INVALID_ARGUMENT;
    *outMeshIndex = vkrt->sceneSettings.selectedMeshIndex;
    return VKRT_SUCCESS;
}
