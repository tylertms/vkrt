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

    if (vkrt->renderStatus.renderModeActive) {
        uint32_t targetFPS = vkrt->sceneSettings.renderModeTargetFPS
            ? vkrt->sceneSettings.renderModeTargetFPS
            : VKRT_DEFAULT_RENDER_MODE_TARGET_FPS;
        return 1000.0f / (float)targetFPS;
    }

    float targetMs = vkrt->autoSPP.targetFrameMs;
    if (targetMs > 0.0f) return targetMs;

    uint32_t targetFPS = vkrt->sceneSettings.autoSPPTargetFPS ? vkrt->sceneSettings.autoSPPTargetFPS : 60u;
    return 1000.0f / (float)targetFPS;
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
    VkResult queryResult = vkGetQueryPoolResults(vkrt->core.device,
        vkrt->runtime.timestampPool,
        frameIndex * 2,
        2,
        sizeof(ts),
        ts,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);
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

    float targetMs = queryAutoSPPTargetMs(vkrt);

    float controlMs = vkrt->renderStatus.displayRenderTimeMs > 0.0f ? vkrt->renderStatus.displayRenderTimeMs : vkrt->renderStatus.displayFrameTimeMs;
    if (controlMs <= 0.0f) return;

    uint32_t spp = vkrt->sceneSettings.samplesPerPixel;
    if (spp == 0) spp = 1;

    float measuredMsPerSPP = controlMs / (float)spp;
    if (measuredMsPerSPP <= 0.0f) return;

    VkBool32 warmup = vkrt->runtime.autoSPPFastAdaptFrames > 0;
    if (warmup) {
        vkrt->runtime.autoSPPFastAdaptFrames--;
    }

    if (vkrt->autoSPP.controlMs <= 0.0f || warmup) {
        vkrt->autoSPP.controlMs = measuredMsPerSPP;
    } else {
        const float smoothing = 0.20f;
        vkrt->autoSPP.controlMs = vkrt->autoSPP.controlMs * (1.0f - smoothing) + measuredMsPerSPP * smoothing;
    }

    float budgetMs = targetMs * 0.95f;
    float desired = budgetMs / vkrt->autoSPP.controlMs;
    if (desired <= 0.0f) return;

    float delta = fabsf(desired - (float)spp) / (float)spp;
    if (!warmup && delta < 0.06f) return;

    float maxStepUp = warmup ? 0.50f : 0.15f;
    float maxStepDown = warmup ? 0.35f : 0.10f;
    float minAllowed = (float)spp * (1.0f - maxStepDown);
    float maxAllowed = (float)spp * (1.0f + maxStepUp);
    float nextValue = glm_clamp(desired, minAllowed, maxAllowed);

    if (nextValue < 1.0f) nextValue = 1.0f;
    if (nextValue > 2048.0f) nextValue = 2048.0f;

    uint32_t next = (uint32_t)(nextValue + 0.5f);
    if (next == spp && desired > (float)spp && spp < 2048) next = spp + 1;
    if (next == spp && desired < (float)spp && spp > 1) next = spp - 1;

    vkrt->autoSPP.framesUntilNextAdjust = 0;

    if (next != spp) {
        VKRT_setSamplesPerPixel(vkrt, next);
    }
}
