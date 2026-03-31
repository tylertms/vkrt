#include "config.h"
#include "debug.h"
#include "platform.h"
#include "scene.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

static void updateFrameTimes(VKRT* vkrt) {
    if (!vkrt) return;

    const float displaySmoothing = 0.12f;
    if (vkrt->renderStatus.displayFrameTimeMs <= 0.0f) {
        vkrt->renderStatus.displayFrameTimeMs = vkrt->renderStatus.displayTimeMs;
    } else {
        vkrt->renderStatus.displayFrameTimeMs = (vkrt->renderStatus.displayFrameTimeMs * (1.0f - displaySmoothing)) +
                                                (vkrt->renderStatus.displayTimeMs * displaySmoothing);
    }

    if (vkrt->renderStatus.displayRenderTimeMs <= 0.0f) {
        vkrt->renderStatus.displayRenderTimeMs = vkrt->renderStatus.renderTimeMs;
    } else {
        vkrt->renderStatus.displayRenderTimeMs = (vkrt->renderStatus.displayRenderTimeMs * (1.0f - displaySmoothing)) +
                                                 (vkrt->renderStatus.renderTimeMs * displaySmoothing);
    }

    size_t sampleCount = (size_t)vkrt->renderStatus.accumulationFrame + 1u;
    size_t frametimeCapacity = sizeof(vkrt->renderStatus.frametimes) / sizeof(vkrt->renderStatus.frametimes[0]);
    if (sampleCount > frametimeCapacity) sampleCount = frametimeCapacity;

    float weight = 1.0f / (float)sampleCount;

    vkrt->renderStatus.averageFrametime =
        (vkrt->renderStatus.averageFrametime * (1.0f - weight)) + (vkrt->renderStatus.displayFrameTimeMs * weight);
    vkrt->renderStatus.framesPerSecond = (uint32_t)(1000.0f / vkrt->renderStatus.displayFrameTimeMs);
    vkrt->renderStatus.frametimes[vkrt->renderControl.timing.frametimeStartIndex] =
        vkrt->renderStatus.displayFrameTimeMs;
    vkrt->renderControl.timing.frametimeStartIndex =
        (vkrt->renderControl.timing.frametimeStartIndex + 1) % frametimeCapacity;
}

static float queryAutoSPPTargetMs(const VKRT* vkrt) {
    if (!vkrt) return 0.0f;

    float targetMs = vkrt->renderControl.autoSPP.targetFrameMs;
    if (targetMs > 0.0f) return targetMs;

    uint32_t targetFPS = vkrt->sceneSettings.autoSPPTargetFPS ? vkrt->sceneSettings.autoSPPTargetFPS : 60u;
    return 1000.0f / (float)targetFPS;
}

static float queryAutoSPPControlMs(const VKRT* vkrt) {
    if (!vkrt) return 0.0f;
    if (vkrt->renderStatus.renderTimeMs > 0.0f) return vkrt->renderStatus.renderTimeMs;
    if (vkrt->renderStatus.displayRenderTimeMs > 0.0f) return vkrt->renderStatus.displayRenderTimeMs;
    return vkrt->renderStatus.displayFrameTimeMs;
}

void resetAutoSPPState(VKRT* vkrt, VkBool32 resetSamplesPerPixel) {
    if (!vkrt) return;

    if (resetSamplesPerPixel && vkrt->sceneSettings.autoSPPEnabled) {
        vkrt->sceneSettings.samplesPerPixel = 1u;
    }

    vkrt->renderControl.autoSPP.controlMs = 0.0f;
    vkrt->renderStatus.displayTimeMs = 0.0f;
    vkrt->renderStatus.renderTimeMs = 0.0f;
    vkrt->renderStatus.displayRenderTimeMs = 0.0f;
    vkrt->renderStatus.displayFrameTimeMs = 0.0f;
    vkrt->renderStatus.framesPerSecond = 0u;
}

void recordFrameTime(VKRT* vkrt, uint32_t frameIndex) {
    if (!vkrt) return;

    uint64_t currentTime = getMicroseconds();
    uint64_t lastFrameTimestamp = vkrt->renderControl.timing.lastFrameTimestamp;

    if (lastFrameTimestamp == 0u) {
        vkrt->renderControl.timing.lastFrameTimestamp = currentTime;
        return;
    }

    double elapsedMicroseconds = (double)(currentTime - lastFrameTimestamp);
    vkrt->renderStatus.displayTimeMs = (float)(elapsedMicroseconds / 1000.0);
    vkrt->renderControl.timing.lastFrameTimestamp = currentTime;

    if (frameIndex >= VKRT_MAX_FRAMES_IN_FLIGHT || !vkrt->runtime.frameTimingPending[frameIndex]) {
        updateFrameTimes(vkrt);
        return;
    }

    uint64_t timestamps[2] = {0u, 0u};
    VkResult queryResult = vkGetQueryPoolResults(
        vkrt->core.device,
        vkrt->runtime.timestampPool,
        frameIndex * 2,
        2,
        sizeof(timestamps),
        timestamps,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT
    );

    if (queryResult != VK_SUCCESS) {
        LOG_ERROR("Failed to collect frame timing query (%d)", (int)queryResult);
        return;
    }

    vkrt->runtime.frameTimingPending[frameIndex] = VK_FALSE;
    double timestampDelta = (double)(timestamps[1] - timestamps[0]);
    double renderTimeMs = (timestampDelta * (double)vkrt->runtime.timestampPeriod) / 1000000.0;
    vkrt->renderStatus.renderTimeMs = (float)renderTimeMs;
    updateFrameTimes(vkrt);
}

void updateAutoSPP(VKRT* vkrt) {
    if (!vkrt || !vkrt->sceneSettings.autoSPPEnabled) return;

    const float measurementSmoothing = 0.35f;
    const float budgetScale = 0.90f;
    const float upwardDeadbandScale = 0.18f;
    const float downwardDeadbandScale = 0.08f;
    const float maxUpwardScale = 1.25f;
    const float maxDownwardScale = 0.60f;

    float targetMs = queryAutoSPPTargetMs(vkrt);
    if (targetMs <= 0.0f) return;

    float controlMs = queryAutoSPPControlMs(vkrt);
    if (controlMs <= 0.0f) return;

    uint32_t spp = vkrt->sceneSettings.samplesPerPixel;
    if (spp == 0) spp = 1;
    float sppf = (float)spp;

    float measuredMsPerSPP = controlMs / sppf;
    if (measuredMsPerSPP <= 0.0f) return;

    if (vkrt->renderControl.autoSPP.controlMs <= 0.0f) {
        vkrt->renderControl.autoSPP.controlMs = measuredMsPerSPP;
    } else {
        vkrt->renderControl.autoSPP.controlMs =
            (vkrt->renderControl.autoSPP.controlMs * (1.0f - measurementSmoothing)) +
            (measuredMsPerSPP * measurementSmoothing);
    }

    float desired = (targetMs * budgetScale) / vkrt->renderControl.autoSPP.controlMs;
    if (desired < 1.0f) desired = 1.0f;
    if (desired > 2048.0f) desired = 2048.0f;

    float delta = desired - sppf;
    float deadband = fmaxf(sppf * (delta > 0.0f ? upwardDeadbandScale : downwardDeadbandScale), 1.0f);
    if (fabsf(delta) <= deadband) return;

    uint32_t next = 0u;
    if (delta > 0.0f) {
        float limitedDesired = fminf(desired, ceilf(sppf * maxUpwardScale));
        next = (uint32_t)floorf(limitedDesired);
        if (next <= spp && spp < 2048u) next = spp + 1u;
    } else {
        float limitedDesired = fmaxf(desired, floorf(sppf * maxDownwardScale));
        next = (uint32_t)ceilf(limitedDesired);
        if (next >= spp && spp > 1u) next = spp - 1u;
    }

    if (next < 1u) next = 1u;
    if (next > 2048u) next = 2048u;

    if (next != spp) {
        vkrt->sceneSettings.samplesPerPixel = next;
        syncSceneStateData(vkrt);
    }
}
