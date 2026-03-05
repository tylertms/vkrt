#include "scene.h"

#include "buffer.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static VKRT_SceneTimelineKeyframe makeDefaultSceneTimelineKeyframe(float time) {
    return (VKRT_SceneTimelineKeyframe){
        .time = time,
        .emissionScale = 1.0f,
        .emissionTint = {1.0f, 1.0f, 1.0f},
    };
}

static void resetTimelineDefaults(VKRT_PublicState* state) {
    if (!state) return;

    state->sceneTimeline.enabled = 0;
    state->sceneTimeline.keyframeCount = 2;
    state->sceneTimeline.keyframes[0] = makeDefaultSceneTimelineKeyframe(0.0f);
    state->sceneTimeline.keyframes[1] = makeDefaultSceneTimelineKeyframe(1.0f);
}

static void writeTimelineUniform(SceneData* sceneData, const VKRT_PublicState* state) {
    if (!sceneData || !state) return;

    sceneData->timelineEnabled = state->sceneTimeline.enabled ? 1u : 0u;
    uint32_t keyCount = state->sceneTimeline.keyframeCount;
    if (keyCount > VKRT_SCENE_TIMELINE_MAX_KEYFRAMES) keyCount = VKRT_SCENE_TIMELINE_MAX_KEYFRAMES;
    sceneData->timelineKeyframeCount = keyCount;

    for (uint32_t i = 0; i < VKRT_SCENE_TIMELINE_MAX_KEYFRAMES; i++) {
        float time = 0.0f;
        float scale = 1.0f;
        vec3 tint = {1.0f, 1.0f, 1.0f};

        if (i < keyCount) {
            VKRT_SceneTimelineKeyframe key = state->sceneTimeline.keyframes[i];
            if (isfinite(key.time)) time = key.time;
            if (isfinite(key.emissionScale)) scale = key.emissionScale;
            for (int c = 0; c < 3; c++) {
                if (isfinite(key.emissionTint[c])) tint[c] = key.emissionTint[c];
            }
        }

        sceneData->timelineTimeScale[i][0] = time;
        sceneData->timelineTimeScale[i][1] = scale;
        sceneData->timelineTimeScale[i][2] = 0.0f;
        sceneData->timelineTimeScale[i][3] = 0.0f;

        sceneData->timelineTint[i][0] = tint[0];
        sceneData->timelineTint[i][1] = tint[1];
        sceneData->timelineTint[i][2] = tint[2];
        sceneData->timelineTint[i][3] = 0.0f;
    }
}

VKRT_Result createSceneUniform(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkDeviceSize uniformBufferSize = sizeof(SceneData);
    if (createBuffer(vkrt,
        uniformBufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &vkrt->core.sceneDataBuffer,
        &vkrt->core.sceneDataMemory) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    if (vkMapMemory(vkrt->core.device, vkrt->core.sceneDataMemory, 0, uniformBufferSize, 0, (void**)&vkrt->core.sceneData) !=
        VK_SUCCESS || !vkrt->core.sceneData) {
        return VKRT_ERROR_OPERATION_FAILED;
    }
    memset(vkrt->core.sceneData, 0, uniformBufferSize);

    VkDeviceSize pickBufferSize = sizeof(PickBuffer);
    if (createBuffer(vkrt,
        pickBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &vkrt->core.pickBuffer.buffer,
        &vkrt->core.pickBuffer.memory) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    if (vkMapMemory(vkrt->core.device, vkrt->core.pickBuffer.memory, 0, pickBufferSize, 0, (void**)&vkrt->core.pickData) !=
        VK_SUCCESS || !vkrt->core.pickData) return VKRT_ERROR_OPERATION_FAILED;
    memset(vkrt->core.pickData, 0, sizeof(*vkrt->core.pickData));
    vkrt->core.pickData->hitMeshIndex = UINT32_MAX;
    vkrt->core.pickBuffer.deviceAddress = 0;
    vkrt->core.pickBuffer.count = 1;

    uint32_t initialWidth = vkrt->runtime.swapChainExtent.width ? vkrt->runtime.swapChainExtent.width : VKRT_DEFAULT_WIDTH;
    uint32_t initialHeight = vkrt->runtime.swapChainExtent.height ? vkrt->runtime.swapChainExtent.height : VKRT_DEFAULT_HEIGHT;
    vkrt->runtime.renderExtent = (VkExtent2D){initialWidth, initialHeight};
    vkrt->runtime.displayViewportRect[0] = 0;
    vkrt->runtime.displayViewportRect[1] = 0;
    vkrt->runtime.displayViewportRect[2] = initialWidth;
    vkrt->runtime.displayViewportRect[3] = initialHeight;

    vkrt->state.samplesPerPixel = 8;
    vkrt->state.rrMaxDepth = 8;
    vkrt->state.rrMinDepth = 4;
    vkrt->state.toneMappingMode = VKRT_TONE_MAPPING_ACES;
    vkrt->state.renderModeActive = 0;
    vkrt->state.renderModeFinished = 0;
    vkrt->state.renderTargetSamples = 0;
    vkrt->state.renderViewZoom = 1.0f;
    vkrt->state.renderViewPanX = 0.0f;
    vkrt->state.renderViewPanY = 0.0f;
    vkrt->state.autoSPPEnabled = 1;
    float refreshHz = vkrt->runtime.displayRefreshHz;
    if (refreshHz <= 0.0f) refreshHz = 60.0f;
    uint32_t targetFPS = (uint32_t)(refreshHz + 0.5f);
    if (targetFPS < 30) targetFPS = 30;
    if (targetFPS > 360) targetFPS = 360;
    vkrt->state.autoSPPTargetFPS = targetFPS;
    vkrt->state.autoSPPTargetFrameMs = 1000.0f / (float)vkrt->state.autoSPPTargetFPS;
    vkrt->state.autoSPPControlMs = 0.0f;
    vkrt->state.autoSPPFramesUntilNextAdjust = 0;

    vkrt->state.camera = (Camera){
        .width = initialWidth, .height = initialHeight,
        .nearZ = 0.001f, .farZ = 10000.0f,
        .vfov = 40.0f,
        .pos = {-0.5f, 0.2f, -0.2f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 0.0f, 1.0f}
    };

    vkrt->core.sceneData->rrMaxDepth = vkrt->state.rrMaxDepth;
    vkrt->core.sceneData->rrMinDepth = vkrt->state.rrMinDepth;
    vkrt->core.sceneData->toneMappingMode = vkrt->state.toneMappingMode;

    vkrt->core.sceneData->viewportRect[0] = 0;
    vkrt->core.sceneData->viewportRect[1] = 0;
    vkrt->core.sceneData->viewportRect[2] = initialWidth;
    vkrt->core.sceneData->viewportRect[3] = initialHeight;

    vkrt->state.timeBase = -1.0f;
    vkrt->state.timeStep = 0.5f;
    vkrt->state.fogDensity = 0.0f;
    vkrt->state.debugMode = 0;
    vkrt->state.misNeeEnabled = 1u;
    vkrt->state.selectionEnabled = 1;
    vkrt->state.selectedMeshIndex = UINT32_MAX;
    resetTimelineDefaults(&vkrt->state);
    vkrt->core.sceneData->timeBase = vkrt->state.timeBase;
    vkrt->core.sceneData->timeStep = vkrt->state.timeStep;
    vkrt->core.sceneData->fogDensity = vkrt->state.fogDensity;
    vkrt->core.sceneData->debugMode = 0;
    vkrt->core.sceneData->misNeeEnabled = 1u;
    vkrt->core.sceneData->emissiveMeshCount = 0;
    vkrt->core.sceneData->emissiveTriangleCount = 0;

    updateMatricesFromCamera(vkrt);
    return VKRT_SUCCESS;
}

void resetSceneData(VKRT* vkrt) {
    vkrt->core.sceneData->frameNumber = 0;
    vkrt->core.sceneData->samplesPerPixel = vkrt->state.samplesPerPixel;
    vkrt->core.sceneData->rrMaxDepth = vkrt->state.rrMaxDepth;
    vkrt->core.sceneData->rrMinDepth = vkrt->state.rrMinDepth;
    vkrt->core.sceneData->toneMappingMode = vkrt->state.toneMappingMode;
    vkrt->core.sceneData->timeBase = vkrt->state.timeBase;
    vkrt->core.sceneData->timeStep = vkrt->state.timeStep;
    vkrt->core.sceneData->fogDensity = vkrt->state.fogDensity;
    vkrt->core.sceneData->debugMode = vkrt->state.debugMode;
    vkrt->core.sceneData->misNeeEnabled = vkrt->state.misNeeEnabled ? 1u : 0u;
    vkrt->core.sceneData->emissiveMeshCount = vkrt->core.emissiveMeshCount;
    vkrt->core.sceneData->emissiveTriangleCount = vkrt->core.emissiveTriangleCount;
    vkrt->core.sceneData->selectionEnabled = vkrt->state.selectionEnabled ? 1u : 0u;
    vkrt->core.sceneData->selectedMeshIndex = vkrt->state.selectedMeshIndex;
    writeTimelineUniform(vkrt->core.sceneData, &vkrt->state);

    vkrt->state.renderModeFinished = 0;
    vkrt->state.accumulationFrame = 0;
    vkrt->state.totalSamples = 0;
    vkrt->state.averageFrametime = 0.0f;
    vkrt->state.frametimeStartIndex = 0;
    vkrt->core.accumulationNeedsReset = VK_TRUE;
    memset(vkrt->state.frametimes, 0, sizeof(vkrt->state.frametimes));
}
