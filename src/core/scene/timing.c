#include "scene.h"

#include "debug.h"

#include <math.h>

static void updateFrameTimes(VKRT* vkrt) {
    if (!vkrt) return;

    const float displaySmoothing = 0.12f;
    if (vkrt->renderStatus.displayFrameTimeMs <= 0.0f) vkrt->renderStatus.displayFrameTimeMs = vkrt->renderStatus.displayTimeMs;
    else vkrt->renderStatus.displayFrameTimeMs =
        vkrt->renderStatus.displayFrameTimeMs * (1.0f - displaySmoothing) + vkrt->renderStatus.displayTimeMs * displaySmoothing;

    if (vkrt->renderStatus.displayRenderTimeMs <= 0.0f) vkrt->renderStatus.displayRenderTimeMs = vkrt->renderStatus.renderTimeMs;
    else vkrt->renderStatus.displayRenderTimeMs =
        vkrt->renderStatus.displayRenderTimeMs * (1.0f - displaySmoothing) + vkrt->renderStatus.renderTimeMs * displaySmoothing;

    size_t n = (size_t)(vkrt->renderStatus.accumulationFrame + 1);
    size_t cap = VKRT_ARRAY_COUNT(vkrt->renderStatus.frametimes);
    if (n > cap) n = cap;

    float weight = 1.0f / (float)n;

    vkrt->renderStatus.averageFrametime =
        vkrt->renderStatus.averageFrametime * (1.0f - weight) + vkrt->renderStatus.displayFrameTimeMs * weight;
    vkrt->renderStatus.framesPerSecond = (uint32_t)(1000.0f / vkrt->renderStatus.displayFrameTimeMs);
    vkrt->renderStatus.frametimes[vkrt->timing.frametimeStartIndex] = vkrt->renderStatus.displayFrameTimeMs;
    vkrt->timing.frametimeStartIndex = (vkrt->timing.frametimeStartIndex + 1) % VKRT_ARRAY_COUNT(vkrt->renderStatus.frametimes);
}

static float queryAutoSPPTargetMs(const VKRT* vkrt) {
    if (!vkrt) return 0.0f;

    if (vkrt->renderStatus.renderModeActive && !vkrt->renderStatus.renderModeFinished) {
        return 1000.0f / (float)VKRT_RENDER_TARGET_FPS;
    }

    float targetMs = vkrt->autoSPP.targetFrameMs;
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

void recordFrameTime(VKRT* vkrt, uint32_t frameIndex) {
    if (!vkrt) return;

    uint64_t currentTime = getMicroseconds();

    if (vkrt->timing.lastFrameTimestamp == 0) {
        vkrt->timing.lastFrameTimestamp = currentTime;
        return;
    }

    vkrt->renderStatus.displayTimeMs = (currentTime - vkrt->timing.lastFrameTimestamp) / 1000.0f;
    vkrt->timing.lastFrameTimestamp = currentTime;

    if (frameIndex >= VKRT_MAX_FRAMES_IN_FLIGHT || !vkrt->runtime.frameTimingPending[frameIndex]) {
        updateFrameTimes(vkrt);
        return;
    }

    uint64_t ts[2] = {0};
    VkResult queryResult = vkGetQueryPoolResults(
        vkrt->core.device,
        vkrt->runtime.timestampPool,
        frameIndex * 2,
        2,
        sizeof(ts),
        ts,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT
    );

    if (queryResult != VK_SUCCESS) {
        LOG_ERROR("Failed to collect frame timing query (%d)", (int)queryResult);
        return;
    }

    vkrt->runtime.frameTimingPending[frameIndex] = VK_FALSE;
    vkrt->renderStatus.renderTimeMs = (float)((ts[1] - ts[0]) * vkrt->runtime.timestampPeriod / 1e6);
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

    if (vkrt->autoSPP.controlMs <= 0.0f) {
        vkrt->autoSPP.controlMs = measuredMsPerSPP;
    } else {
        vkrt->autoSPP.controlMs =
            vkrt->autoSPP.controlMs * (1.0f - measurementSmoothing) + measuredMsPerSPP * measurementSmoothing;
    }

    float desired = (targetMs * budgetScale) / vkrt->autoSPP.controlMs;
    if (desired < 1.0f) desired = 1.0f;
    if (desired > 2048.0f) desired = 2048.0f;

    float delta = desired - sppf;
    float deadband = fmaxf(sppf * (delta > 0.0f ? upwardDeadbandScale : downwardDeadbandScale), 1.0f);
    if (fabsf(delta) <= deadband) return;

    float limitedDesired = desired;
    uint32_t next = spp;
    if (delta > 0.0f) {
        limitedDesired = fminf(desired, ceilf(sppf * maxUpwardScale));
        next = (uint32_t)floorf(limitedDesired);
        if (next <= spp && spp < 2048u) next = spp + 1u;
    } else {
        limitedDesired = fmaxf(desired, floorf(sppf * maxDownwardScale));
        next = (uint32_t)ceilf(limitedDesired);
        if (next >= spp && spp > 1u) next = spp - 1u;
    }

    if (next < 1u) next = 1u;
    if (next > 2048u) next = 2048u;

    if (next != spp) {
        VKRT_setSamplesPerPixel(vkrt, next);
    }
}
