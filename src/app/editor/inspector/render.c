#include "common.h"
#include "config.h"
#include "session.h"
#include "vkrt.h"

#include "IconsFontAwesome6.h"
#include "debug.h"

#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

static const float kMicrosecondsPerSecond = 1000000.0f;
static const int kRenderOutputDimensionMin = 1;
static const int kRenderOutputDimensionMax = 16384;

enum {
    kRenderTimeTextCapacity = 32,
    kRenderProgressOverlayCapacity = 96,
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

    uint8_t renderModeFinished = status->renderModeActive && status->renderModeFinished;
    if (status->renderModeActive && !timer->wasActive) {
        timer->startTimeUs = nowUs;
        timer->completedSeconds = 0.0f;
    }

    if (status->renderModeActive && renderModeFinished && timer->completedSeconds <= 0.0f &&
        timer->startTimeUs > 0 && nowUs >= timer->startTimeUs) {
        timer->completedSeconds = (float)(nowUs - timer->startTimeUs) / kMicrosecondsPerSecond;
    }

    if (!status->renderModeActive && timer->wasActive) {
        if (timer->completedSeconds <= 0.0f && timer->startTimeUs > 0 && nowUs >= timer->startTimeUs) {
            timer->completedSeconds = (float)(nowUs - timer->startTimeUs) / kMicrosecondsPerSecond;
        }
        timer->startTimeUs = 0;
    }

    timer->wasActive = status->renderModeActive;
}

static float queryActiveRenderSeconds(const SessionRenderTimer* timer, uint64_t nowUs) {
    if (!timer || timer->startTimeUs == 0 || nowUs < timer->startTimeUs) return 0.0f;
    return (float)(nowUs - timer->startTimeUs) / kMicrosecondsPerSecond;
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

static void drawIdleOutputSection(Session* session) {
    if (!session) return;

    int outputSize[2] = {
        (int)session->editor.renderConfig.width,
        (int)session->editor.renderConfig.height
    };
    int targetSamples = (int)session->editor.renderConfig.targetSamples;

    if (!inspectorBeginCollapsingHeaderSection("Output", ImGuiTreeNodeFlags_DefaultOpen)) return;

    inspectorIndentSection();
    if (ImGui_DragInt2Ex("Output Size", outputSize, 1.0f, kRenderOutputDimensionMin, kRenderOutputDimensionMax, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        session->editor.renderConfig.width = clampRenderDimension(outputSize[0]);
        session->editor.renderConfig.height = clampRenderDimension(outputSize[1]);
    }

    if (ImGui_DragIntEx("Samples", &targetSamples, 1.0f, 0, INT_MAX, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        if (targetSamples < 0) targetSamples = 0;
        session->editor.renderConfig.targetSamples = (uint32_t)targetSamples;
    }
    tooltipOnHover("Total samples to render. Set to 0 for manual stop.");
    inspectorUnindentSection();
    inspectorEndCollapsingHeaderSection();
}

static void drawIdleRenderState(Session* session, const SessionRenderTimer* timer) {
    drawIdleOutputSection(session);

    if (inspectorPaddedButton(ICON_FA_CAMERA " Start Render")) {
        sessionQueueRenderStart(
            session,
            session->editor.renderConfig.width,
            session->editor.renderConfig.height,
            session->editor.renderConfig.targetSamples
        );
    }

    if (timer->completedSeconds > 0.0f) {
        char totalText[kRenderTimeTextCapacity];
        formatTime(timer->completedSeconds, totalText, sizeof(totalText));
        ImGui_TextDisabled(ICON_FA_CLOCK " Last render: %s", totalText);
    }
}

static void drawRenderProgressSection(
    const VKRT_RenderStatusSnapshot* status,
    const VKRT_RuntimeSnapshot* runtime,
    float elapsedActiveSeconds,
    float completedSeconds
) {
    if (!status || !runtime) return;

    ImGui_TextDisabled("Output %ux%u", runtime->renderWidth, runtime->renderHeight);
    if (status->renderTargetSamples > 0) {
        uint64_t shownSamples = status->totalSamples;
        if (shownSamples > status->renderTargetSamples) {
            shownSamples = status->renderTargetSamples;
        }
        float progress = (float)shownSamples / (float)status->renderTargetSamples;
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;
        char overlay[kRenderProgressOverlayCapacity];
        snprintf(
            overlay,
            sizeof(overlay),
            "%" PRIu64 " / %u samples",
            shownSamples,
            status->renderTargetSamples
        );
        ImGui_ProgressBar(progress, (ImVec2){-1.0f, 0.0f}, overlay);
    } else {
        char overlay[kRenderProgressOverlayCapacity];
        snprintf(
            overlay,
            sizeof(overlay),
            "%" PRIu64 " samples",
            status->totalSamples
        );
        ImGui_ProgressBar(0.0f, (ImVec2){-1.0f, 0.0f}, overlay);
    }

    if (status->renderModeFinished) {
        float elapsedDoneSeconds = completedSeconds > 0.0f ? completedSeconds : elapsedActiveSeconds;
        char elapsedText[kRenderTimeTextCapacity];
        formatTime(fmaxf(elapsedDoneSeconds, 0.0f), elapsedText, sizeof(elapsedText));
        ImGui_Text(ICON_FA_CHECK " Complete  " ICON_FA_CLOCK " %s", elapsedText);
    } else {
        float etaSeconds = -1.0f;
        if (status->renderTargetSamples > 0 && status->totalSamples > 0 && elapsedActiveSeconds > 0.0f) {
            float samplesPerSecond = (float)status->totalSamples / elapsedActiveSeconds;
            if (samplesPerSecond > 0.0f && status->renderTargetSamples > status->totalSamples) {
                etaSeconds = (float)(status->renderTargetSamples - status->totalSamples) / samplesPerSecond;
            }
        }

        if (etaSeconds >= 0.0f) {
            char etaText[kRenderTimeTextCapacity];
            formatTime(etaSeconds, etaText, sizeof(etaText));
            ImGui_Text("Rendering  " ICON_FA_CLOCK " ETA %s", etaText);
        } else {
            ImGui_Text("Rendering...");
        }
    }
}

static void drawRenderActionsSection(VKRT* vkrt, Session* session, const VKRT_RenderStatusSnapshot* status) {
    if (!vkrt || !session || !status) return;

    ImGui_Spacing();

    if (!status->renderModeFinished) {
        if (inspectorPaddedButton(ICON_FA_STOP " Stop Render")) {
            VKRT_Result result = VKRT_stopRenderSampling(vkrt);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Stopping render sampling failed (%d)", (int)result);
            }
        }
    } else {
        if (inspectorPaddedButton(ICON_FA_FLOPPY_DISK " Save Image")) {
            sessionRequestRenderSaveDialog(session);
        }
        ImGui_SameLine();
        if (inspectorPaddedButton(ICON_FA_ARROW_RIGHT_FROM_BRACKET " Exit Render")) {
            sessionQueueRenderStop(session);
        }
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

    if (!status.renderModeActive) {
        drawIdleRenderState(session, timer);
        return;
    }

    float elapsedActiveSeconds = queryActiveRenderSeconds(timer, nowUs);
    drawRenderProgressSection(&status, &runtime, elapsedActiveSeconds, timer->completedSeconds);
    drawRenderActionsSection(vkrt, session, &status);
}
