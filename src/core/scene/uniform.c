#include "scene.h"

#include "buffer.h"

#include <stdint.h>
#include <string.h>

static VKRT_SceneTimelineKeyframe makeDefaultSceneTimelineKeyframe(float time) {
    return (VKRT_SceneTimelineKeyframe){
        .time = time,
        .emissionScale = 1.0f,
        .emissionTint = {1.0f, 1.0f, 1.0f},
    };
}

static void resetTimelineDefaults(VKRT_SceneSettingsSnapshot* settings) {
    settings->sceneTimeline.enabled = 0;
    settings->sceneTimeline.keyframeCount = 2;
    settings->sceneTimeline.keyframes[0] = makeDefaultSceneTimelineKeyframe(0.0f);
    settings->sceneTimeline.keyframes[1] = makeDefaultSceneTimelineKeyframe(1.0f);
}

static void resetAutoSPPForSceneChange(VKRT* vkrt) {
    if (!vkrt->sceneSettings.autoSPPEnabled) return;

    vkrt->sceneSettings.samplesPerPixel = 1u;
    vkrt->renderControl.autoSPP.controlMs = 0.0f;
}

static void resetAutoExposureForSceneChange(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->renderControl.autoExposure.filteredLuminance = 0.0f;
    for (uint32_t i = 0; i < VKRT_MAX_FRAMES_IN_FLIGHT; i++) {
        vkrt->renderControl.autoExposure.readbacks[i].pending = 0u;
    }
}

static void writeTimelineUniform(SceneData* sceneData, const VKRT_SceneSettingsSnapshot* settings) {
    sceneData->timelineEnabled = settings->sceneTimeline.enabled ? 1u : 0u;
    uint32_t keyCount = settings->sceneTimeline.keyframeCount;
    if (keyCount > VKRT_SCENE_TIMELINE_MAX_KEYFRAMES) keyCount = VKRT_SCENE_TIMELINE_MAX_KEYFRAMES;
    sceneData->timelineKeyframeCount = keyCount;

    for (uint32_t i = 0; i < VKRT_SCENE_TIMELINE_MAX_KEYFRAMES; i++) {
        float time = 0.0f;
        float scale = 1.0f;
        vec3 tint = {1.0f, 1.0f, 1.0f};

        if (i < keyCount) {
            VKRT_SceneTimelineKeyframe key = settings->sceneTimeline.keyframes[i];
            time = key.time;
            scale = key.emissionScale;
            for (int c = 0; c < 3; c++) {
                tint[c] = key.emissionTint[c];
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

static void writeSceneStateUniform(SceneData* sceneData, const VKRT* vkrt) {
    const VKRT_SceneSettingsSnapshot* settings = &vkrt->sceneSettings;
    sceneData->samplesPerPixel = settings->samplesPerPixel > 0u ? settings->samplesPerPixel : 1u;
    sceneData->rrMaxDepth = settings->rrMaxDepth;
    sceneData->rrMinDepth = settings->rrMinDepth;
    sceneData->toneMappingMode = settings->toneMappingMode;
    sceneData->exposure = settings->exposure;
    sceneData->timeBase = settings->timeBase;
    sceneData->timeStep = settings->timeStep;
    sceneData->environmentLight[0] = settings->environmentColor[0] * settings->environmentStrength;
    sceneData->environmentLight[1] = settings->environmentColor[1] * settings->environmentStrength;
    sceneData->environmentLight[2] = settings->environmentColor[2] * settings->environmentStrength;
    sceneData->environmentLight[3] = settings->environmentStrength;
    sceneData->environmentTextureIndex = settings->environmentTextureIndex;
    sceneData->debugMode = settings->debugMode;
    sceneData->misNeeEnabled = settings->misNeeEnabled ? 1u : 0u;
    sceneData->selectionEnabled = settings->selectionEnabled ? 1u : 0u;
    sceneData->selectedMeshIndex = settings->selectedMeshIndex;
    writeTimelineUniform(sceneData, settings);
}

static void syncSceneDataToFrame(VKRT* vkrt, uint32_t frameIndex) {
    if (!vkrt->core.sceneData || frameIndex >= VKRT_MAX_FRAMES_IN_FLIGHT) return;
    if (!vkrt->core.sceneFrameData[frameIndex]) return;
    memcpy(vkrt->core.sceneFrameData[frameIndex], vkrt->core.sceneData, sizeof(*vkrt->core.sceneData));
}

static void syncAllSceneDataFrames(VKRT* vkrt) {
    for (uint32_t frameIndex = 0; frameIndex < VKRT_MAX_FRAMES_IN_FLIGHT; frameIndex++) {
        syncSceneDataToFrame(vkrt, frameIndex);
    }
}

void syncCurrentFrameSceneData(VKRT* vkrt) {
    if (!vkrt) return;
    syncSceneDataToFrame(vkrt, vkrt->runtime.currentFrame);
}

void syncSceneStateData(VKRT* vkrt) {
    if (!vkrt || !vkrt->core.sceneData) return;
    writeSceneStateUniform(vkrt->core.sceneData, vkrt);
}

void markSelectionMaskDirty(VKRT* vkrt) {
    if (!vkrt) return;
    vkrt->core.selectionMaskDirty = VK_TRUE;
}

VKRT_Result createSceneUniform(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VkDeviceSize uniformBufferSize = sizeof(SceneData);
    vkrt->core.sceneData = &vkrt->core.sceneDataHost;
    memset(vkrt->core.sceneData, 0, sizeof(*vkrt->core.sceneData));

    for (uint32_t frameIndex = 0; frameIndex < VKRT_MAX_FRAMES_IN_FLIGHT; frameIndex++) {
        if (createBuffer(
            vkrt,
            uniformBufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &vkrt->core.sceneDataBuffers[frameIndex],
            &vkrt->core.sceneDataMemories[frameIndex]
        ) != VKRT_SUCCESS) {
            return VKRT_ERROR_OPERATION_FAILED;
        }

        if (vkMapMemory(
            vkrt->core.device,
            vkrt->core.sceneDataMemories[frameIndex],
            0,
            uniformBufferSize,
            0,
            (void**)&vkrt->core.sceneFrameData[frameIndex]
        ) != VK_SUCCESS ||
            !vkrt->core.sceneFrameData[frameIndex]) {
            return VKRT_ERROR_OPERATION_FAILED;
        }

        memset(vkrt->core.sceneFrameData[frameIndex], 0, uniformBufferSize);
    }

    VkDeviceSize selectionSize = sizeof(Selection);
    if (createBuffer(
        vkrt,
        selectionSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &vkrt->core.selection.buffer,
        &vkrt->core.selection.memory
    ) != VKRT_SUCCESS) return VKRT_ERROR_OPERATION_FAILED;

    if (vkMapMemory(vkrt->core.device, vkrt->core.selection.memory, 0, selectionSize, 0, (void**)&vkrt->core.selectionData) !=
        VK_SUCCESS || !vkrt->core.selectionData) return VKRT_ERROR_OPERATION_FAILED;

    memset(vkrt->core.selectionData, 0, sizeof(*vkrt->core.selectionData));
    vkrt->core.selectionData->hitMeshIndex = VKRT_INVALID_INDEX;
    vkrt->core.selectionPendingFrame = 0;
    vkrt->core.selectionResultMeshIndex = VKRT_INVALID_INDEX;
    vkrt->core.selectionPending = 0;
    vkrt->core.selectionSubmitted = 0;
    vkrt->core.selectionResultReady = 0;
    markSelectionMaskDirty(vkrt);
    vkrt->core.selection.deviceAddress = 0;
    vkrt->core.selection.count = 1;

    if (createAutoExposureReadbacks(vkrt) != VKRT_SUCCESS) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint32_t initialWidth = vkrt->runtime.swapChainExtent.width ? vkrt->runtime.swapChainExtent.width : VKRT_DEFAULT_WIDTH;
    uint32_t initialHeight = vkrt->runtime.swapChainExtent.height ? vkrt->runtime.swapChainExtent.height : VKRT_DEFAULT_HEIGHT;
    vkrt->runtime.renderExtent = (VkExtent2D){initialWidth, initialHeight};
    vkrt->runtime.displayViewportRect[0] = 0;
    vkrt->runtime.displayViewportRect[1] = 0;
    vkrt->runtime.displayViewportRect[2] = initialWidth;
    vkrt->runtime.displayViewportRect[3] = initialHeight;

    vkrt->sceneSettings.samplesPerPixel = 8;
    vkrt->sceneSettings.rrMaxDepth = 8;
    vkrt->sceneSettings.rrMinDepth = 4;
    vkrt->sceneSettings.toneMappingMode = VKRT_TONE_MAPPING_MODE_ACES;
    vkrt->sceneSettings.exposure = 1.0f;
    vkrt->sceneSettings.autoExposureEnabled = 0u;
    vkrt->sceneSettings.environmentColor[0] = 0.25f;
    vkrt->sceneSettings.environmentColor[1] = 0.25f;
    vkrt->sceneSettings.environmentColor[2] = 0.25f;
    vkrt->sceneSettings.environmentStrength = 1.0f;
    vkrt->sceneSettings.environmentTextureIndex = VKRT_INVALID_INDEX;
    vkrt->renderStatus.renderPhase = VKRT_RENDER_PHASE_INACTIVE;
    vkrt->renderStatus.renderDenoiseEnabled = vkrt->renderControl.finalImageDenoiseEnabled ? 1u : 0u;
    vkrt->renderStatus.renderTargetSamples = 0;
    vkrt->renderControl.view.zoom = 1.0f;
    vkrt->renderControl.view.panX = 0.0f;
    vkrt->renderControl.view.panY = 0.0f;
    vkrt->sceneSettings.autoSPPEnabled = 1;
    float refreshHz = vkrt->runtime.displayRefreshHz;
    if (refreshHz <= 0.0f) refreshHz = 60.0f;
    uint32_t targetFPS = (uint32_t)(refreshHz + 0.5f);
    if (targetFPS < 30) targetFPS = 30;
    if (targetFPS > 360) targetFPS = 360;
    vkrt->sceneSettings.autoSPPTargetFPS = targetFPS;
    vkrt->renderControl.autoSPP.targetFrameMs = 1000.0f / (float)targetFPS;
    vkrt->renderControl.autoSPP.controlMs = 0.0f;

    vkrt->sceneSettings.camera = (Camera){
        .nearZ = 0.001f, .farZ = 10000.0f,
        .vfov = 40.0f,
        .pos = {-0.5f, 0.2f, -0.2f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 0.0f, 1.0f}
    };

    vkrt->core.sceneData->viewportRect[0] = 0;
    vkrt->core.sceneData->viewportRect[1] = 0;
    vkrt->core.sceneData->viewportRect[2] = initialWidth;
    vkrt->core.sceneData->viewportRect[3] = initialHeight;

    vkrt->sceneSettings.timeBase = -1.0f;
    vkrt->sceneSettings.timeStep = 0.5f;
    vkrt->sceneSettings.debugMode = VKRT_DEBUG_MODE_NONE;
    vkrt->sceneSettings.misNeeEnabled = 1u;
    vkrt->sceneSettings.selectionEnabled = 0;
    vkrt->sceneSettings.selectedMeshIndex = VKRT_INVALID_INDEX;
    resetTimelineDefaults(&vkrt->sceneSettings);

    syncCameraMatrices(vkrt);
    syncSceneStateData(vkrt);
    syncAllSceneDataFrames(vkrt);
    return VKRT_SUCCESS;
}

void resetSceneData(VKRT* vkrt) {
    if (!vkrt || !vkrt->core.sceneData) return;

    resetAutoSPPForSceneChange(vkrt);
    resetAutoExposureForSceneChange(vkrt);
    vkrt->core.sceneData->frameNumber = 0;
    vkrt->renderStatus.renderPhase = VKRT_renderPhaseIsActive(vkrt->renderStatus.renderPhase)
        ? VKRT_RENDER_PHASE_SAMPLING
        : VKRT_RENDER_PHASE_INACTIVE;
    vkrt->renderStatus.accumulationFrame = 0;
    vkrt->renderStatus.totalSamples = 0;
    vkrt->renderStatus.averageFrametime = 0.0f;
    vkrt->renderControl.timing.frametimeStartIndex = 0;
    vkrt->core.accumulationNeedsReset = VK_TRUE;
    syncSceneStateData(vkrt);
    vkrt->core.sceneData->emissiveMeshCount = vkrt->core.emissiveMeshCount;
    vkrt->core.sceneData->emissiveTriangleCount = vkrt->core.emissiveTriangleCount;
    memset(vkrt->renderStatus.frametimes, 0, sizeof(vkrt->renderStatus.frametimes));
}

void syncSelectionSceneData(VKRT* vkrt) {
    if (!vkrt || !vkrt->core.sceneData) return;

    vkrt->core.sceneData->selectionEnabled = vkrt->sceneSettings.selectionEnabled ? 1u : 0u;
    vkrt->core.sceneData->selectedMeshIndex = vkrt->sceneSettings.selectedMeshIndex;
    markSelectionMaskDirty(vkrt);
    syncAllSceneDataFrames(vkrt);
}
