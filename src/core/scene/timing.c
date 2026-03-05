#include "scene.h"

#include "debug.h"

#include <math.h>

void recordFrameTime(VKRT* vkrt) {
    uint64_t currentTime = getMicroseconds();

    if (vkrt->state.lastFrameTimestamp == 0) {
        vkrt->state.lastFrameTimestamp = currentTime;
        return;
    }

    vkrt->state.displayTimeMs = (currentTime - vkrt->state.lastFrameTimestamp) / 1000.0f;
    vkrt->state.lastFrameTimestamp = currentTime;

    uint64_t ts[2];
    vkGetQueryPoolResults(vkrt->core.device,
        vkrt->runtime.timestampPool,
        vkrt->runtime.currentFrame * 2,
        2,
        sizeof(ts),
        ts,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    vkrt->state.renderTimeMs = (float)((ts[1] - ts[0]) * vkrt->runtime.timestampPeriod / 1e6);

    const float displaySmoothing = 0.12f;
    if (vkrt->state.displayFrameTimeMs <= 0.0f) vkrt->state.displayFrameTimeMs = vkrt->state.displayTimeMs;
    else vkrt->state.displayFrameTimeMs =
        vkrt->state.displayFrameTimeMs * (1.0f - displaySmoothing) + vkrt->state.displayTimeMs * displaySmoothing;

    if (vkrt->state.displayRenderTimeMs <= 0.0f) vkrt->state.displayRenderTimeMs = vkrt->state.renderTimeMs;
    else vkrt->state.displayRenderTimeMs =
        vkrt->state.displayRenderTimeMs * (1.0f - displaySmoothing) + vkrt->state.renderTimeMs * displaySmoothing;

    size_t n = (size_t)(vkrt->state.accumulationFrame + 1);
    size_t cap = VKRT_ARRAY_COUNT(vkrt->state.frametimes);
    if (n > cap) n = cap;

    float weight = 1.0f / (float)n;

    vkrt->state.averageFrametime =
        vkrt->state.averageFrametime * (1.0f - weight) + vkrt->state.displayFrameTimeMs * weight;
    vkrt->state.framesPerSecond = (uint32_t)(1000.0f / vkrt->state.displayFrameTimeMs);
    vkrt->state.frametimes[vkrt->state.frametimeStartIndex] = vkrt->state.displayFrameTimeMs;
    vkrt->state.frametimeStartIndex = (vkrt->state.frametimeStartIndex + 1) % VKRT_ARRAY_COUNT(vkrt->state.frametimes);
}

void updateAutoSPP(VKRT* vkrt) {
    if (!vkrt || !vkrt->state.autoSPPEnabled) return;

    float targetMs = vkrt->state.autoSPPTargetFrameMs;
    if (targetMs <= 0.0f) {
        uint32_t targetFPS = vkrt->state.autoSPPTargetFPS ? vkrt->state.autoSPPTargetFPS : 60;
        targetMs = 1000.0f / (float)targetFPS;
    }

    float controlMs = vkrt->state.displayRenderTimeMs > 0.0f ? vkrt->state.displayRenderTimeMs : vkrt->state.displayFrameTimeMs;
    if (controlMs <= 0.0f) return;

    uint32_t spp = vkrt->state.samplesPerPixel;
    if (spp == 0) spp = 1;

    float measuredMsPerSPP = controlMs / (float)spp;
    if (measuredMsPerSPP <= 0.0f) return;

    VkBool32 warmup = vkrt->runtime.autoSPPFastAdaptFrames > 0;
    if (warmup) {
        vkrt->runtime.autoSPPFastAdaptFrames--;
    }

    if (vkrt->state.autoSPPControlMs <= 0.0f || warmup) {
        vkrt->state.autoSPPControlMs = measuredMsPerSPP;
    } else {
        const float smoothing = 0.20f;
        vkrt->state.autoSPPControlMs = vkrt->state.autoSPPControlMs * (1.0f - smoothing) + measuredMsPerSPP * smoothing;
    }

    float budgetMs = targetMs * 0.95f;
    float desired = budgetMs / vkrt->state.autoSPPControlMs;
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

    vkrt->state.autoSPPFramesUntilNextAdjust = 0;

    if (next != spp) {
        VKRT_setSamplesPerPixel(vkrt, next);
    }
}
