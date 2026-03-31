#include "IconsFontAwesome6.h"
#include "common.h"
#include "config.h"
#include "platform.h"
#include "sections.h"
#include "session.h"
#include "vkrt.h"
#include "vkrt_types.h"

#include <dcimgui.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static const float kMicrosecondsPerSecond = 1000000.0f;
static const int kRenderOutputDimensionMin = 1;
static const int kRenderOutputDimensionMax = 16384;

enum {
    K_RENDER_TIME_TEXT_CAPACITY = 32,
    K_RENDER_PROGRESS_OVERLAY_CAPACITY = 96,
};

static void initializeRenderConfig(VKRT* vkrt, Session* session) {
    if (!session) return;
    if (session->editor.renderConfig.width == 0 || session->editor.renderConfig.height == 0) {
        VKRT_RuntimeSnapshot runtime = {0};
        uint32_t width = VKRT_DEFAULT_WIDTH;
        uint32_t height = VKRT_DEFAULT_HEIGHT;
        if (vkrt && VKRT_getRuntimeSnapshot(vkrt, &runtime) == VKRT_SUCCESS) {
            if (runtime.displayWidth > 0 && runtime.displayHeight > 0) {
                width = runtime.displayWidth;
                height = runtime.displayHeight;
            } else if (runtime.displayViewportRect[2] > 0 && runtime.displayViewportRect[3] > 0) {
                width = runtime.displayViewportRect[2];
                height = runtime.displayViewportRect[3];
            } else if (runtime.swapchainWidth > 0 && runtime.swapchainHeight > 0) {
                width = runtime.swapchainWidth;
                height = runtime.swapchainHeight;
            } else if (runtime.renderWidth > 0 && runtime.renderHeight > 0) {
                width = runtime.renderWidth;
                height = runtime.renderHeight;
            }
        }
        session->editor.renderConfig.width = width;
        session->editor.renderConfig.height = height;
    }
}

static void updateRenderTimer(const VKRT_RenderStatusSnapshot* status, SessionRenderTimer* timer, uint64_t nowUs) {
    if (!status || !timer) return;

    uint8_t renderModeActive = VKRT_renderStatusIsActive(status);
    uint8_t renderModeFinished = VKRT_renderStatusIsComplete(status);
    if (renderModeActive && !timer->wasActive) {
        timer->startTimeUs = nowUs;
        timer->accumulatedSeconds = 0.0f;
        timer->completedSeconds = 0.0f;
    }

    if (renderModeActive && timer->wasActive && timer->wasComplete && !renderModeFinished) {
        timer->startTimeUs = nowUs;
        timer->completedSeconds = 0.0f;
    }

    if (renderModeActive && renderModeFinished && !timer->wasComplete) {
        if (timer->startTimeUs > 0 && nowUs >= timer->startTimeUs) {
            timer->accumulatedSeconds += (float)(nowUs - timer->startTimeUs) / kMicrosecondsPerSecond;
        }
        timer->startTimeUs = 0u;
        timer->completedSeconds = timer->accumulatedSeconds;
    }

    if (!renderModeActive && timer->wasActive) {
        if (!timer->wasComplete && timer->startTimeUs > 0 && nowUs >= timer->startTimeUs) {
            timer->accumulatedSeconds += (float)(nowUs - timer->startTimeUs) / kMicrosecondsPerSecond;
        }
        timer->startTimeUs = 0u;
        timer->completedSeconds = timer->accumulatedSeconds;
    }

    timer->wasActive = renderModeActive;
    timer->wasComplete = renderModeFinished;
}

static float queryActiveRenderSeconds(const SessionRenderTimer* timer, uint64_t nowUs) {
    float elapsedSeconds = 0.0f;

    if (!timer) return 0.0f;

    elapsedSeconds = timer->accumulatedSeconds;
    if (timer->startTimeUs == 0 || nowUs < timer->startTimeUs) {
        return elapsedSeconds;
    }

    return elapsedSeconds + ((float)(nowUs - timer->startTimeUs) / kMicrosecondsPerSecond);
}

void inspectorPrepareRenderState(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    initializeRenderConfig(vkrt, session);

    VKRT_RenderStatusSnapshot status = {0};
    if (VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS) return;

    uint64_t nowUs = getMicroseconds();
    SessionRenderTimer* timer = &session->runtime.renderTimer;
    updateRenderTimer(&status, timer, nowUs);
}

static void drawIdleOutputControls(Session* session) {
    if (!session) return;

    int outputSize[2] = {(int)session->editor.renderConfig.width, (int)session->editor.renderConfig.height};
    int targetSamples = (int)session->editor.renderConfig.targetSamples;

    if (ImGui_DragInt2Ex(
            "Output Size",
            outputSize,
            1.0f,
            kRenderOutputDimensionMin,
            kRenderOutputDimensionMax,
            "%d",
            ImGuiSliderFlags_AlwaysClamp
        )) {
        session->editor.renderConfig.width = clampRenderDimension(outputSize[0]);
        session->editor.renderConfig.height = clampRenderDimension(outputSize[1]);
    }

    if (ImGui_DragIntEx("Samples", &targetSamples, 1.0f, 0, INT_MAX, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        if (targetSamples < 0) targetSamples = 0;
        session->editor.renderConfig.targetSamples = (uint32_t)targetSamples;
    }
    tooltipOnHover("0 runs until stopped.");
}

static void drawFinalImageControls(Session* session, const VKRT_RenderStatusSnapshot* status) {
    if (!session) return;
    if (status && !VKRT_renderStatusIsSampling(status)) return;

    bool denoiseEnabled = session->editor.renderExportSettings.denoiseEnabled != 0u;

    if (ImGui_Checkbox("Denoise (OIDN)", &denoiseEnabled)) {
        session->editor.renderExportSettings.denoiseEnabled = (int)denoiseEnabled ? 1u : 0u;
        if (status) {
            sessionQueueRenderSetDenoise(session, session->editor.renderExportSettings.denoiseEnabled);
        }
    }
    tooltipOnHover("CPU denoiser using color, albedo, and normal buffers.");
}

static void drawIdleRenderState(Session* session, const SessionRenderTimer* timer) {
    drawIdleOutputControls(session);
    drawFinalImageControls(session, NULL);

    if (inspectorPaddedButton(ICON_FA_CAMERA " Start Render")) {
        SessionRenderSettings settings = {
            .width = session->editor.renderConfig.width,
            .height = session->editor.renderConfig.height,
            .targetSamples = session->editor.renderConfig.targetSamples,
            .denoiseEnabled = session->editor.renderExportSettings.denoiseEnabled,
        };
        sessionQueueRenderStart(session, &settings);
    }

    if (timer->completedSeconds > 0.0f) {
        char totalText[K_RENDER_TIME_TEXT_CAPACITY];
        formatTime(timer->completedSeconds, totalText, sizeof(totalText));
        ImGui_TextDisabled(ICON_FA_CLOCK " Last render: %s", totalText);
    }
}

static float queryRenderProgressFraction(const VKRT_RenderStatusSnapshot* status, uint64_t shownSamples) {
    float progress = 0.0f;

    if (!status) return 0.0f;
    if (VKRT_renderStatusIsDenoising(status)) return 1.0f;
    if (status->renderTargetSamples == 0u) {
        return VKRT_renderStatusIsBusy(status) ? 1.0f : 0.0f;
    }

    progress = (float)shownSamples / (float)status->renderTargetSamples;
    if (progress < 0.0f) return 0.0f;
    if (progress > 1.0f) return 1.0f;
    return progress;
}

static void formatRenderProgressOverlay(
    const VKRT_RenderStatusSnapshot* status,
    char* out,
    size_t outSize,
    uint64_t* outShownSamples
) {
    uint64_t shownSamples = 0u;

    if (!status || !out || outSize == 0u) return;

    shownSamples = status->totalSamples;
    if (status->renderTargetSamples > 0u && shownSamples > status->renderTargetSamples) {
        shownSamples = status->renderTargetSamples;
    }

    if (VKRT_renderStatusIsDenoising(status)) {
        (void)snprintf(out, outSize, "Denoising...");
    } else if (status->renderTargetSamples > 0u) {
        (void)
            snprintf(out, outSize, "%llu / %u samples", (unsigned long long)shownSamples, status->renderTargetSamples);
    } else {
        (void)snprintf(out, outSize, "%llu samples", (unsigned long long)status->totalSamples);
    }

    if (outShownSamples) *outShownSamples = shownSamples;
}

static float queryRenderEtaSeconds(const VKRT_RenderStatusSnapshot* status, float elapsedActiveSeconds) {
    float samplesPerSecond = 0.0f;

    if (!status || status->renderTargetSamples == 0u || status->totalSamples == 0u || elapsedActiveSeconds <= 0.0f) {
        return -1.0f;
    }

    samplesPerSecond = (float)status->totalSamples / elapsedActiveSeconds;
    if (samplesPerSecond <= 0.0f || status->renderTargetSamples <= status->totalSamples) {
        return -1.0f;
    }
    return (float)(status->renderTargetSamples - status->totalSamples) / samplesPerSecond;
}

static uint32_t queryContinueRenderTargetSamples(const VKRT_RenderStatusSnapshot* status, uint32_t configuredSamples) {
    uint64_t continuedTarget = 0u;

    if (!status) return configuredSamples;
    if (status->renderTargetSamples == 0u) return 0u;
    if (status->totalSamples < (uint64_t)status->renderTargetSamples) {
        return status->renderTargetSamples;
    }
    if (configuredSamples == 0u) return 0u;

    continuedTarget = status->totalSamples + (uint64_t)configuredSamples;
    return continuedTarget > UINT32_MAX ? 0u : (uint32_t)continuedTarget;
}

static int renderContinueExtendsPastCurrentTarget(const VKRT_RenderStatusSnapshot* status) {
    return status && status->renderTargetSamples > 0u && status->totalSamples >= (uint64_t)status->renderTargetSamples;
}

static const char* queryContinueRenderButtonLabel(const VKRT_RenderStatusSnapshot* status) {
    return renderContinueExtendsPastCurrentTarget(status) ? ICON_FA_FORWARD " Continue Render"
                                                          : ICON_FA_PLAY " Resume Render";
}

static void drawContinueRenderControls(Session* session, const VKRT_RenderStatusSnapshot* status) {
    if (!session || !status || !VKRT_renderStatusIsComplete(status)) return;

    if (renderContinueExtendsPastCurrentTarget(status)) {
        int additionalSamples = (int)session->editor.renderConfig.targetSamples;
        if (ImGui_DragIntEx(
                "Additional Samples",
                &additionalSamples,
                1.0f,
                0,
                INT_MAX,
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            )) {
            if (additionalSamples < 0) additionalSamples = 0;
            session->editor.renderConfig.targetSamples = (uint32_t)additionalSamples;
        }
        tooltipOnHover("Used when extending a completed render. 0 continues until stopped.");
        return;
    }

    if (status->renderTargetSamples == 0u) {
        ImGui_TextDisabled("Resume will continue until stopped.");
        return;
    }

    ImGui_TextDisabled("Resume will continue toward %u samples.", status->renderTargetSamples);
}

static void drawRenderProgressStatusText(
    const VKRT_RenderStatusSnapshot* status,
    float elapsedActiveSeconds,
    float completedSeconds
) {
    if (VKRT_renderStatusIsDenoising(status)) {
        ImGui_Text("Denoising...");
        return;
    }

    if (VKRT_renderStatusIsComplete(status)) {
        float elapsedDoneSeconds = completedSeconds > 0.0f ? completedSeconds : elapsedActiveSeconds;
        char elapsedText[K_RENDER_TIME_TEXT_CAPACITY];
        formatTime(fmaxf(elapsedDoneSeconds, 0.0f), elapsedText, sizeof(elapsedText));
        ImGui_Text(ICON_FA_CHECK " Complete  " ICON_FA_CLOCK " %s", elapsedText);
        return;
    }

    float etaSeconds = queryRenderEtaSeconds(status, elapsedActiveSeconds);
    if (etaSeconds >= 0.0f) {
        char etaText[K_RENDER_TIME_TEXT_CAPACITY];
        formatTime(etaSeconds, etaText, sizeof(etaText));
        ImGui_Text("Rendering  " ICON_FA_CLOCK " ETA %s", etaText);
        return;
    }

    ImGui_Text("Rendering...");
}

static void drawRenderProgressSection(
    const VKRT_RenderStatusSnapshot* status,
    const VKRT_RuntimeSnapshot* runtime,
    float elapsedActiveSeconds,
    float completedSeconds
) {
    if (!status || !runtime) return;

    ImGui_TextDisabled("Output %ux%u", runtime->renderWidth, runtime->renderHeight);
    {
        char overlay[K_RENDER_PROGRESS_OVERLAY_CAPACITY];
        float progress = 0.0f;
        uint64_t shownSamples = 0u;

        formatRenderProgressOverlay(status, overlay, sizeof(overlay), &shownSamples);
        progress = queryRenderProgressFraction(status, shownSamples);
        ImGui_ProgressBar(progress, (ImVec2){-1.0f, 0.0f}, overlay);
    }
    drawRenderProgressStatusText(status, elapsedActiveSeconds, completedSeconds);
}

static void drawRenderActionsSection(Session* session, const VKRT_RenderStatusSnapshot* status) {
    if (!session || !status) return;

    ImGui_Spacing();

    if (VKRT_renderStatusIsSampling(status)) {
        if (inspectorPaddedButton(ICON_FA_STOP " Stop Render")) {
            sessionQueueRenderStopSampling(session, session->editor.renderExportSettings.denoiseEnabled);
        }
        return;
    }

    if (VKRT_renderStatusIsBusy(status)) {
        return;
    }

    drawContinueRenderControls(session, status);
    ImGui_Spacing();

    if (status->renderPhase == VKRT_RENDER_PHASE_COMPLETE_RAW) {
        if (inspectorPaddedButton("Denoise")) {
            session->editor.renderExportSettings.denoiseEnabled = 1u;
            sessionQueueRenderDenoise(session);
        }
        ImGui_SameLine();
    }

    if (inspectorPaddedButton(queryContinueRenderButtonLabel(status))) {
        SessionRenderSettings settings = {
            .targetSamples = queryContinueRenderTargetSamples(status, session->editor.renderConfig.targetSamples),
            .denoiseEnabled = session->editor.renderExportSettings.denoiseEnabled,
        };
        sessionQueueRenderContinue(session, &settings);
    }
    ImGui_SameLine();
    if (inspectorPaddedButton(ICON_FA_FLOPPY_DISK " Save Image")) {
        sessionRequestRenderSaveDialog(session);
    }
    ImGui_SameLine();
    if (inspectorPaddedButton(ICON_FA_ARROW_RIGHT_FROM_BRACKET " Exit Render")) {
        sessionQueueRenderStop(session);
    }
}

void inspectorDrawRenderTab(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;
    inspectorPrepareRenderState(vkrt, session);

    VKRT_RenderStatusSnapshot status = {0};
    VKRT_RuntimeSnapshot runtime = {0};
    if (VKRT_getRenderStatus(vkrt, &status) != VKRT_SUCCESS ||
        VKRT_getRuntimeSnapshot(vkrt, &runtime) != VKRT_SUCCESS) {
        return;
    }

    uint64_t nowUs = getMicroseconds();
    SessionRenderTimer* timer = &session->runtime.renderTimer;

    if (!VKRT_renderStatusIsActive(&status)) {
        drawIdleRenderState(session, timer);
        return;
    }

    float elapsedActiveSeconds = queryActiveRenderSeconds(timer, nowUs);
    drawRenderProgressSection(&status, &runtime, elapsedActiveSeconds, timer->completedSeconds);
    drawFinalImageControls(session, &status);
    drawRenderActionsSection(session, &status);
}
