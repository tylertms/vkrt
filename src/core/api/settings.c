#include "constants.h"
#include "scene.h"
#include "state.h"
#include "vkrt_internal.h"
#include "numeric.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t sanitizeMeshSelection(const VKRT* vkrt, uint32_t meshIndex) {
    if (!vkrt || meshIndex == VKRT_INVALID_INDEX) return VKRT_INVALID_INDEX;
    return meshIndex < vkrt->core.meshCount ? meshIndex : VKRT_INVALID_INDEX;
}

static void cancelPendingPick(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.pickPending = 0;
    vkrt->core.pickSubmitted = 0;
    vkrt->core.pickResultReady = 0;
    vkrt->core.pickResultMeshIndex = VKRT_INVALID_INDEX;
    if (vkrt->core.pickData) {
        vkrt->core.pickData->hitMeshIndex = VKRT_INVALID_INDEX;
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
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (samplesPerPixel == 0) samplesPerPixel = 1;

    if (vkrt->sceneSettings.samplesPerPixel == samplesPerPixel) return VKRT_SUCCESS;
    vkrt->sceneSettings.samplesPerPixel = samplesPerPixel;

    if (vkrt->core.sceneData) {
        syncSceneStateData(vkrt);
        syncCurrentFrameSceneData(vkrt);
    }
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setPathDepth(VKRT* vkrt, uint32_t rrMinDepth, uint32_t rrMaxDepth) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    if (rrMaxDepth < 1u) rrMaxDepth = 1u;
    if (rrMaxDepth > 64u) rrMaxDepth = 64u;
    if (rrMinDepth > rrMaxDepth) rrMinDepth = rrMaxDepth;

    if (vkrt->sceneSettings.rrMinDepth == rrMinDepth && vkrt->sceneSettings.rrMaxDepth == rrMaxDepth) return VKRT_SUCCESS;

    vkrt->sceneSettings.rrMinDepth = rrMinDepth;
    vkrt->sceneSettings.rrMaxDepth = rrMaxDepth;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setAutoSPPEnabled(VKRT* vkrt, uint8_t enabled) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    vkrt->sceneSettings.autoSPPEnabled = enabled ? 1 : 0;
    vkrt->autoSPP.controlMs = 0.0f;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setAutoSPPTargetFPS(VKRT* vkrt, uint32_t targetFPS) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    targetFPS = sanitizeAutoSPPTargetFPS(vkrt, targetFPS);
    vkrt->sceneSettings.autoSPPTargetFPS = targetFPS;
    vkrt->autoSPP.targetFrameMs = 1000.0f / (float)targetFPS;
    vkrt->autoSPP.controlMs = 0.0f;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setToneMappingMode(VKRT* vkrt, VKRT_ToneMappingMode toneMappingMode) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    if (vkrt->sceneSettings.toneMappingMode == toneMappingMode) return VKRT_SUCCESS;
    vkrt->sceneSettings.toneMappingMode = toneMappingMode;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setFogDensity(VKRT* vkrt, float fogDensity) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    fogDensity = vkrtFiniteClampf(fogDensity, 0.0f, 0.0f, INFINITY);

    if (vkrt->sceneSettings.fogDensity == fogDensity) return VKRT_SUCCESS;
    vkrt->sceneSettings.fogDensity = fogDensity;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setDebugMode(VKRT* vkrt, uint32_t mode) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
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

VKRT_Result VKRT_setSceneTimeline(VKRT* vkrt, const VKRT_SceneTimelineSettings* timeline) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    VKRT_SceneTimelineSettings sanitized = {0};

    if (timeline) {
        sanitized.enabled = timeline->enabled ? 1u : 0u;
        uint32_t keyCount = timeline->keyframeCount;
        if (keyCount > VKRT_SCENE_TIMELINE_MAX_KEYFRAMES) {
            keyCount = VKRT_SCENE_TIMELINE_MAX_KEYFRAMES;
        }

        sanitized.keyframeCount = keyCount;
        for (uint32_t keyIndex = 0; keyIndex < keyCount; keyIndex++) {
            VKRT_SceneTimelineKeyframe key = timeline->keyframes[keyIndex];
            key.time = vkrtFiniteOrf(key.time, 0.0f);
            key.emissionScale = vkrtFiniteClampf(key.emissionScale, 1.0f, 0.0f, INFINITY);

            for (int channel = 0; channel < 3; channel++) {
                key.emissionTint[channel] = vkrtFiniteClampf(key.emissionTint[channel], 1.0f, 0.0f, INFINITY);
            }

            sanitized.keyframes[keyIndex] = key;
        }

        if (sanitized.keyframeCount > 1) {
            qsort(sanitized.keyframes,
                sanitized.keyframeCount,
                sizeof(sanitized.keyframes[0]),
                vkrtCompareSceneTimelineKeyframesByTime);
        }
    }

    if (memcmp(&vkrt->sceneSettings.sceneTimeline, &sanitized, sizeof(sanitized)) == 0) return VKRT_SUCCESS;
    vkrt->sceneSettings.sceneTimeline = sanitized;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_requestPickAtPixel(VKRT* vkrt, uint32_t x, uint32_t y) {
    if (!vkrt || !vkrt->core.pickData) return VKRT_ERROR_INVALID_ARGUMENT;
    if (x > 0xFFFFu || y > 0xFFFFu) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrt->core.pickPending) return VKRT_SUCCESS;

    PickBuffer* pickData = vkrt->core.pickData;
    pickData->pixel = (x & 0xFFFFu) | ((y & 0xFFFFu) << 16);
    pickData->hitMeshIndex = VKRT_INVALID_INDEX;
    vkrt->core.pickPendingFrame = vkrt->runtime.currentFrame;
    vkrt->core.pickPending = 1;
    vkrt->core.pickSubmitted = 0;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_consumePickedMesh(VKRT* vkrt, uint32_t* outMeshIndex, uint8_t* outReady) {
    if (!vkrt || !outMeshIndex || !outReady) return VKRT_ERROR_INVALID_ARGUMENT;

    *outMeshIndex = VKRT_INVALID_INDEX;
    *outReady = 0;
    if (!vkrt->core.pickResultReady) return VKRT_SUCCESS;

    *outMeshIndex = vkrt->core.pickResultMeshIndex;
    *outReady = 1;
    vkrt->core.pickResultReady = 0;
    vkrt->core.pickResultMeshIndex = VKRT_INVALID_INDEX;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setSelectedMesh(VKRT* vkrt, uint32_t meshIndex) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    uint32_t nextSelectedMesh = sanitizeMeshSelection(vkrt, meshIndex);
    cancelPendingPick(vkrt);
    if (vkrt->sceneSettings.selectedMeshIndex == nextSelectedMesh) return VKRT_SUCCESS;

    vkrt->sceneSettings.selectedMeshIndex = nextSelectedMesh;
    vkrt->sceneSettings.selectionEnabled = nextSelectedMesh != VKRT_INVALID_INDEX;

    syncSelectionSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getSelectedMesh(const VKRT* vkrt, uint32_t* outMeshIndex) {
    if (!vkrt || !outMeshIndex) return VKRT_ERROR_INVALID_ARGUMENT;
    *outMeshIndex = vkrt->sceneSettings.selectedMeshIndex;
    return VKRT_SUCCESS;
}
