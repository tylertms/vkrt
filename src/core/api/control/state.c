#include "scene.h"
#include "vkrt_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int compareTimelineKeyframesByTime(const void* lhs, const void* rhs) {
    const VKRT_SceneTimelineKeyframe* a = (const VKRT_SceneTimelineKeyframe*)lhs;
    const VKRT_SceneTimelineKeyframe* b = (const VKRT_SceneTimelineKeyframe*)rhs;
    if (a->time < b->time) return -1;
    if (a->time > b->time) return 1;
    return 0;
}

static uint32_t sanitizeMeshSelection(const VKRT* vkrt, uint32_t meshIndex) {
    if (!vkrt || meshIndex == UINT32_MAX) return UINT32_MAX;
    return meshIndex < vkrt->core.meshData.count ? meshIndex : UINT32_MAX;
}

VKRT_Result VKRT_applyCameraInput(VKRT* vkrt, const VKRT_CameraInput* input) {
    if (!vkrt || !input) return VKRT_ERROR_INVALID_ARGUMENT;
    applyCameraInput(vkrt, input);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_invalidateAccumulation(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setSamplesPerPixel(VKRT* vkrt, uint32_t samplesPerPixel) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (samplesPerPixel == 0) samplesPerPixel = 1;

    if (vkrt->state.samplesPerPixel == samplesPerPixel) return VKRT_SUCCESS;
    vkrt->state.samplesPerPixel = samplesPerPixel;

    if (vkrt->core.sceneData) {
        vkrt->core.sceneData->samplesPerPixel = samplesPerPixel;
    }
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setPathDepth(VKRT* vkrt, uint32_t rrMinDepth, uint32_t rrMaxDepth) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (rrMaxDepth < 1u) rrMaxDepth = 1u;
    if (rrMaxDepth > 64u) rrMaxDepth = 64u;
    if (rrMinDepth > rrMaxDepth) rrMinDepth = rrMaxDepth;

    if (vkrt->state.rrMinDepth == rrMinDepth && vkrt->state.rrMaxDepth == rrMaxDepth) return VKRT_SUCCESS;

    vkrt->state.rrMinDepth = rrMinDepth;
    vkrt->state.rrMaxDepth = rrMaxDepth;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setAutoSPPEnabled(VKRT* vkrt, uint8_t enabled) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    vkrt->state.autoSPPEnabled = enabled ? 1 : 0;
    vkrt->state.autoSPPControlMs = 0.0f;
    vkrt->state.autoSPPFramesUntilNextAdjust = 0;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setAutoSPPTargetFPS(VKRT* vkrt, uint32_t targetFPS) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (targetFPS == 0) {
        float hz = vkrt->runtime.displayRefreshHz;
        if (hz <= 0.0f) hz = 60.0f;
        targetFPS = (uint32_t)(hz + 0.5f);
    }

    if (targetFPS < 30) targetFPS = 30;
    if (targetFPS > 360) targetFPS = 360;
    vkrt->state.autoSPPTargetFPS = targetFPS;
    vkrt->state.autoSPPTargetFrameMs = 1000.0f / (float)targetFPS;
    vkrt->state.autoSPPControlMs = 0.0f;
    vkrt->state.autoSPPFramesUntilNextAdjust = 0;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setToneMappingMode(VKRT* vkrt, VKRT_ToneMappingMode toneMappingMode) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (vkrt->state.toneMappingMode == toneMappingMode) return VKRT_SUCCESS;
    vkrt->state.toneMappingMode = toneMappingMode;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setFogDensity(VKRT* vkrt, float fogDensity) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!isfinite(fogDensity) || fogDensity < 0.0f) fogDensity = 0.0f;

    if (vkrt->state.fogDensity == fogDensity) return VKRT_SUCCESS;
    vkrt->state.fogDensity = fogDensity;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setDebugMode(VKRT* vkrt, uint32_t mode) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrt->state.debugMode == mode) return VKRT_SUCCESS;
    vkrt->state.debugMode = mode;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setMISNEEEnabled(VKRT* vkrt, uint32_t enabled) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    enabled = enabled ? 1u : 0u;
    if (vkrt->state.misNeeEnabled == enabled) return VKRT_SUCCESS;
    vkrt->state.misNeeEnabled = enabled;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setTimeRange(VKRT* vkrt, float timeBase, float timeStep) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    if (timeBase < 0.0f) {
        timeBase = -1.0f;
        timeStep = -1.0f;
    } else if (timeStep < timeBase) {
        timeStep = timeBase;
    }

    if (vkrt->state.timeBase == timeBase && vkrt->state.timeStep == timeStep) return VKRT_SUCCESS;

    vkrt->state.timeBase = timeBase;
    vkrt->state.timeStep = timeStep;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setSceneTimeline(VKRT* vkrt, const VKRT_SceneTimelineSettings* timeline) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

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
            if (!isfinite(key.time)) key.time = 0.0f;
            if (!isfinite(key.emissionScale)) key.emissionScale = 1.0f;
            if (key.emissionScale < 0.0f) key.emissionScale = 0.0f;

            for (int channel = 0; channel < 3; channel++) {
                if (!isfinite(key.emissionTint[channel])) key.emissionTint[channel] = 1.0f;
                if (key.emissionTint[channel] < 0.0f) key.emissionTint[channel] = 0.0f;
            }

            sanitized.keyframes[keyIndex] = key;
        }

        if (sanitized.keyframeCount > 1) {
            qsort(sanitized.keyframes,
                sanitized.keyframeCount,
                sizeof(sanitized.keyframes[0]),
                compareTimelineKeyframesByTime);
        }
    }

    if (memcmp(&vkrt->state.sceneTimeline, &sanitized, sizeof(sanitized)) == 0) return VKRT_SUCCESS;
    vkrt->state.sceneTimeline = sanitized;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_pickMeshAtPixel(const VKRT* vkrt, uint32_t x, uint32_t y, uint32_t* outMeshIndex) {
    if (!vkrt || !outMeshIndex || !vkrt->core.pickData) return VKRT_ERROR_INVALID_ARGUMENT;
    *outMeshIndex = UINT32_MAX;
    if (x > 0xFFFFu || y > 0xFFFFu) return VKRT_ERROR_INVALID_ARGUMENT;

    PickBuffer* pickData = vkrt->core.pickData;
    pickData->pixel = (x & 0xFFFFu) | ((y & 0xFFFFu) << 16);

    uint32_t requestID = pickData->requestID + 1u;
    if (requestID == 0u) requestID = 1u;
    pickData->requestID = requestID;

    if (pickData->resultID != requestID) return VKRT_SUCCESS;
    *outMeshIndex = sanitizeMeshSelection(vkrt, pickData->hitMeshIndex);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setSelectedMesh(VKRT* vkrt, uint32_t meshIndex) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint32_t nextSelectedMesh = sanitizeMeshSelection(vkrt, meshIndex);
    if (vkrt->state.selectedMeshIndex == nextSelectedMesh) return VKRT_SUCCESS;

    vkrt->state.selectedMeshIndex = nextSelectedMesh;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_getSelectedMesh(const VKRT* vkrt, uint32_t* outMeshIndex) {
    if (!vkrt || !outMeshIndex) return VKRT_ERROR_INVALID_ARGUMENT;
    *outMeshIndex = vkrt->state.selectedMeshIndex;
    return VKRT_SUCCESS;
}
