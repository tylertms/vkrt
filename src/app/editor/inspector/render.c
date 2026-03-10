#include "common.h"
#include "config.h"

#include "IconsFontAwesome6.h"
#include "debug.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>

static void initializeRenderConfig(VKRT* vkrt, Session* session) {
    if (!session) return;
    if (session->editor.renderConfig.width == 0 || session->editor.renderConfig.height == 0) {
        VKRT_RuntimeSnapshot runtime = {0};
        uint32_t width = VKRT_DEFAULT_WIDTH;
        uint32_t height = VKRT_DEFAULT_HEIGHT;
        if (vkrt && VKRT_getRuntimeSnapshot(vkrt, &runtime) == VKRT_SUCCESS) {
            if (runtime.displayViewportRect[2] > 0 && runtime.displayViewportRect[3] > 0) {
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
    sessionSanitizeAnimationSettings(&session->editor.renderConfig.animation);
}

static void updateRenderTimer(const VKRT_PublicState* state, SessionRenderTimer* timer, uint64_t nowUs) {
    if (!state || !timer) return;

    uint8_t renderModeActive = state->renderModeActive != 0;
    uint8_t renderModeFinished = (renderModeActive && state->renderModeFinished != 0) ? 1u : 0u;

    if (renderModeActive && !timer->wasActive) {
        timer->startTimeUs = nowUs;
        timer->completedSeconds = 0.0f;
    }

    if (renderModeActive && renderModeFinished && timer->completedSeconds <= 0.0f &&
        timer->startTimeUs > 0 && nowUs >= timer->startTimeUs) {
        timer->completedSeconds = (float)(nowUs - timer->startTimeUs) / 1000000.0f;
    }

    if (!renderModeActive && timer->wasActive) {
        if (timer->completedSeconds <= 0.0f && timer->startTimeUs > 0 && nowUs >= timer->startTimeUs) {
            timer->completedSeconds = (float)(nowUs - timer->startTimeUs) / 1000000.0f;
        }
        timer->startTimeUs = 0;
    }

    timer->wasActive = renderModeActive;
}

static float queryActiveRenderSeconds(const SessionRenderTimer* timer, uint64_t nowUs) {
    if (!timer || timer->startTimeUs == 0 || nowUs < timer->startTimeUs) return 0.0f;
    return (float)(nowUs - timer->startTimeUs) / 1000000.0f;
}

void inspectorPrepareRenderState(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    initializeRenderConfig(vkrt, session);

    const VKRT_PublicState* state = VKRT_getPublicState(vkrt);
    if (!state) return;

    uint64_t nowUs = getMicroseconds();
    SessionRenderTimer* timer = &session->runtime.renderTimer;
    updateRenderTimer(state, timer, nowUs);
}

static void drawIdleOutputSection(Session* session) {
    int outputSize[2] = {
        (int)session->editor.renderConfig.width,
        (int)session->editor.renderConfig.height
    };
    int targetSamples = (int)session->editor.renderConfig.targetSamples;

    if (!ImGui_CollapsingHeader("Output", ImGuiTreeNodeFlags_DefaultOpen)) return;

    inspectorIndentSection();
    inspectorPushWidgetSpacing();
    if (ImGui_DragInt2Ex("Output Size", outputSize, 1.0f, 1, 16384, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        session->editor.renderConfig.width = clampRenderDimension(outputSize[0]);
        session->editor.renderConfig.height = clampRenderDimension(outputSize[1]);
    }

    if (ImGui_DragIntEx("Samples", &targetSamples, 1.0f, 0, INT_MAX, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        if (targetSamples < 0) targetSamples = 0;
        session->editor.renderConfig.targetSamples = (uint32_t)targetSamples;
    }
    tooltipOnHover("Total samples to render. Set to 0 for manual stop.");
    inspectorPopWidgetSpacing();
    inspectorUnindentSection();
}

static void drawTimelineEditor(SessionRenderAnimationSettings* animation) {
    SessionSceneTimelineSettings* sceneTimeline = &animation->sceneTimeline;
    bool timelineEnabled = sceneTimeline->enabled != 0;

    if (!ImGui_CollapsingHeader("Timeline", ImGuiTreeNodeFlags_None)) return;

    if (ImGui_Checkbox("Enabled##render_timeline_enabled", &timelineEnabled)) {
        sceneTimeline->enabled = timelineEnabled ? 1 : 0;
        sessionSanitizeAnimationSettings(animation);
    }
    tooltipOnHover("Step emission values at discrete points in source time.");

    ImGui_BeginDisabled(!timelineEnabled);
    bool timelineEdited = false;
    bool timeActivelyEditing = false;

    if (inspectorPaddedButton(ICON_FA_PLUS " Add")) {
        if (sceneTimeline->keyframeCount < SESSION_SCENE_TIMELINE_KEYFRAME_CAPACITY) {
            SessionSceneTimelineKeyframe newKey = {
                .time = 0.0f,
                .emissionScale = 1.0f,
                .emissionTint = {1.0f, 1.0f, 1.0f},
            };
            if (sceneTimeline->keyframeCount > 0) {
                newKey = sceneTimeline->keyframes[sceneTimeline->keyframeCount - 1];
                newKey.time += SESSION_SCENE_TIMELINE_DEFAULT_INCREMENT;
            }
            sceneTimeline->keyframes[sceneTimeline->keyframeCount] = newKey;
            sceneTimeline->keyframeCount++;
            timelineEdited = true;
        }
    }

    if (sceneTimeline->keyframeCount > 1) {
        ImGui_SameLine();
        if (inspectorPaddedButton(ICON_FA_MINUS " Remove")) {
            sceneTimeline->keyframeCount--;
            timelineEdited = true;
        }
    }

    for (uint32_t keyIndex = 0; keyIndex < sceneTimeline->keyframeCount; keyIndex++) {
        SessionSceneTimelineKeyframe* key = &sceneTimeline->keyframes[keyIndex];
        ImGui_PushIDInt((int)keyIndex);
        ImGui_SeparatorText("Marker");

        ImGui_DragFloatEx("Time", &key->time, 0.01f,
            SESSION_SCENE_TIMELINE_TIME_MIN,
            SESSION_SCENE_TIMELINE_TIME_MAX,
            "%.3f",
            ImGuiSliderFlags_AlwaysClamp);
        timelineEdited |= ImGui_IsItemDeactivatedAfterEdit();
        timeActivelyEditing |= ImGui_IsItemActive();

        timelineEdited |= ImGui_DragFloatEx("Emission Scale", &key->emissionScale, 0.01f,
            SESSION_SCENE_TIMELINE_EMISSION_SCALE_MIN,
            SESSION_SCENE_TIMELINE_EMISSION_SCALE_MAX,
            "%.3f",
            ImGuiSliderFlags_AlwaysClamp);

        inspectorPushWidgetSpacing();
        timelineEdited |= ImGui_ColorEdit3("Emission Tint", key->emissionTint, ImGuiColorEditFlags_Float);
        inspectorPopWidgetSpacing();
        ImGui_PopID();
    }

    if (timelineEdited && !timeActivelyEditing) {
        sessionSanitizeAnimationSettings(animation);
    }

    ImGui_EndDisabled();
}

static void drawIdleAnimationSection(Session* session) {
    SessionRenderAnimationSettings* animation = &session->editor.renderConfig.animation;
    bool animationEnabled = animation->enabled != 0;
    uint32_t frameCount = sessionComputeAnimationFrameCount(animation);
    const char* sequenceFolder = sessionGetRenderSequenceFolder(session);
    if (!sequenceFolder || !sequenceFolder[0]) sequenceFolder = "(not set)";

    if (!ImGui_CollapsingHeader("Sequence", ImGuiTreeNodeFlags_None)) return;

    inspectorIndentSection();
    inspectorPushWidgetSpacing();

    if (ImGui_Checkbox("Enabled##render_animation_enabled", &animationEnabled)) {
        animation->enabled = animationEnabled ? 1 : 0;
        if (!animationEnabled) {
            sessionSanitizeAnimationSettings(animation);
        }
    }
    tooltipOnHover("Render a sequence by stepping light travel time from Min to Max.");

    float timeMin = animation->minTime;
    float timeMax = animation->maxTime;
    float timeStep = animation->timeStep;

    ImGui_BeginDisabled(!animationEnabled);

    if (ImGui_DragFloatEx("Time Min", &timeMin, 0.01f, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
        animation->minTime = timeMin;
        sessionSanitizeAnimationSettings(animation);
        frameCount = sessionComputeAnimationFrameCount(animation);
    }

    if (ImGui_DragFloatEx("Time Max", &timeMax, 0.01f, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
        animation->maxTime = timeMax;
        sessionSanitizeAnimationSettings(animation);
        frameCount = sessionComputeAnimationFrameCount(animation);
    }

    if (ImGui_DragFloatEx("Step", &timeStep, 0.005f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
        animation->timeStep = timeStep;
        sessionSanitizeAnimationSettings(animation);
        frameCount = sessionComputeAnimationFrameCount(animation);
    }

    ImGui_TextDisabled("Frames: %u", frameCount);

    if (inspectorPaddedButton(ICON_FA_FOLDER_OPEN " Select Folder")) {
        sessionRequestRenderSequenceFolderDialog(session);
    }

    char folderPathBuffer[256] = {0};
    snprintf(folderPathBuffer, sizeof(folderPathBuffer), "%s", sequenceFolder);
    ImGui_InputTextEx("Folder", folderPathBuffer, sizeof(folderPathBuffer), ImGuiInputTextFlags_ReadOnly, NULL, NULL);
    tooltipOnHover(sequenceFolder);

    drawTimelineEditor(animation);

    if (session->editor.renderConfig.targetSamples == 0) {
        ImGui_Spacing();
        ImGui_TextWrapped("Sequence mode needs finite samples. 0 will be promoted to 1.");
    }

    ImGui_EndDisabled();
    inspectorPopWidgetSpacing();
    inspectorUnindentSection();
}

static void drawIdleRenderState(VKRT* vkrt, Session* session, const SessionRenderTimer* timer) {
    const VKRT_PublicState* state = VKRT_getPublicState(vkrt);
    if (!state) return;

    bool animationEnabled = session->editor.renderConfig.animation.enabled != 0;

    drawIdleOutputSection(session);
    drawIdleAnimationSection(session);

    ImGui_Spacing();

    const char* startLabel = animationEnabled
        ? ICON_FA_CLAPPERBOARD " Start Sequence"
        : ICON_FA_CAMERA " Start Render";
    if (inspectorPaddedButton(startLabel)) {
        uint32_t startSamples = session->editor.renderConfig.targetSamples;
        if (animationEnabled && startSamples == 0) startSamples = 1;
        sessionQueueRenderStart(session,
            session->editor.renderConfig.width,
            session->editor.renderConfig.height,
            startSamples,
            &session->editor.renderConfig.animation);
    }

    if (timer->completedSeconds > 0.0f) {
        char totalText[32] = {0};
        formatTime(timer->completedSeconds, totalText, sizeof(totalText));
        ImGui_TextDisabled(ICON_FA_CLOCK " Last render: %s", totalText);
    }
}

static void drawRenderProgressSection(
    const VKRT_PublicState* state,
    const VKRT_RuntimeSnapshot* runtime,
    const SessionSequenceProgress* sequence,
    float elapsedActiveSeconds,
    float completedSeconds
) {
    if (!state || !runtime || !sequence) return;

    ImGui_PushStyleColorImVec4(ImGuiCol_FrameBg, kProgressBgColor);
    ImGui_PushStyleColorImVec4(ImGuiCol_PlotHistogram, kProgressFillColor);
    ImGui_PushStyleColorImVec4(ImGuiCol_Text, kProgressTextColor);

    ImGui_TextDisabled("Output %ux%u", runtime->renderWidth, runtime->renderHeight);

    if (state->renderTargetSamples > 0) {
        uint64_t shownSamples = state->totalSamples;
        if (shownSamples > state->renderTargetSamples) {
            shownSamples = state->renderTargetSamples;
        }
        float progress = (float)shownSamples / (float)state->renderTargetSamples;
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;
        char overlay[96] = {0};
        snprintf(overlay, sizeof(overlay), "%llu / %u samples",
            (unsigned long long)shownSamples, state->renderTargetSamples);
        ImGui_ProgressBar(progress, (ImVec2){-1.0f, 0.0f}, overlay);
    } else {
        char overlay[96] = {0};
        snprintf(overlay, sizeof(overlay), "%llu samples",
            (unsigned long long)state->totalSamples);
        ImGui_ProgressBar(0.0f, (ImVec2){-1.0f, 0.0f}, overlay);
    }

    if (sequence->active) {
        float sequenceProgress = 0.0f;
        if (sequence->frameCount > 0u) {
            uint32_t frameShown = sequence->frameIndex;
            if (frameShown > sequence->frameCount) frameShown = sequence->frameCount;
            sequenceProgress = (float)frameShown / (float)sequence->frameCount;
        }
        if (sequenceProgress < 0.0f) sequenceProgress = 0.0f;
        if (sequenceProgress > 1.0f) sequenceProgress = 1.0f;

        char sequenceOverlay[96] = {0};
        snprintf(sequenceOverlay, sizeof(sequenceOverlay), "Frame %u / %u",
            sequence->frameIndex, sequence->frameCount);
        ImGui_ProgressBar(sequenceProgress, (ImVec2){-1.0f, 0.0f}, sequenceOverlay);
    }

    ImGui_PopStyleColorEx(3);

    ImGui_Spacing();

    if (state->renderModeFinished) {
        float elapsedDoneSeconds = completedSeconds > 0.0f ? completedSeconds : elapsedActiveSeconds;
        char elapsedText[32] = {0};
        formatTime(fmaxf(elapsedDoneSeconds, 0.0f), elapsedText, sizeof(elapsedText));
        ImGui_Text(ICON_FA_CHECK " Complete  " ICON_FA_CLOCK " %s", elapsedText);
    } else {
        float etaSeconds = -1.0f;
        if (sequence->active && sequence->hasEstimatedRemaining) {
            etaSeconds = fmaxf(sequence->estimatedRemainingSeconds, 0.0f);
        } else if (state->renderTargetSamples > 0 && state->totalSamples > 0 && elapsedActiveSeconds > 0.0f) {
            float samplesPerSecond = (float)state->totalSamples / elapsedActiveSeconds;
            if (samplesPerSecond > 0.0f && state->renderTargetSamples > state->totalSamples) {
                etaSeconds = (float)(state->renderTargetSamples - state->totalSamples) / samplesPerSecond;
            }
        }

        if (etaSeconds >= 0.0f) {
            char etaText[32] = {0};
            formatTime(etaSeconds, etaText, sizeof(etaText));
            ImGui_Text("Rendering  " ICON_FA_CLOCK " ETA %s", etaText);
        } else {
            ImGui_Text("Rendering...");
        }
    }
}

static void drawRenderActionsSection(VKRT* vkrt, Session* session, const VKRT_PublicState* state, const SessionSequenceProgress* sequence) {
    if (!vkrt || !session || !state || !sequence) return;

    ImGui_Spacing();

    if (!state->renderModeFinished) {
        if (sequence->active) {
            if (inspectorPaddedButton(ICON_FA_STOP " Stop Sequence")) {
                sessionQueueRenderStop(session);
            }
        } else if (inspectorPaddedButton(ICON_FA_STOP " Stop Render")) {
            VKRT_Result result = VKRT_stopRenderSampling(vkrt);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Stopping render sampling failed (%d)", (int)result);
            }
        }
    } else {
        if (!sequence->active) {
            if (inspectorPaddedButton(ICON_FA_FLOPPY_DISK " Save Image")) {
                sessionRequestRenderSaveDialog(session);
            }
            ImGui_SameLine();
        }
        if (inspectorPaddedButton(ICON_FA_ARROW_RIGHT_FROM_BRACKET " Exit Render")) {
            sessionQueueRenderStop(session);
        }
    }
}

void inspectorDrawRenderTab(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;
    inspectorPrepareRenderState(vkrt, session);

    const VKRT_PublicState* state = VKRT_getPublicState(vkrt);
    VKRT_RuntimeSnapshot runtime = {0};
    if (!state || VKRT_getRuntimeSnapshot(vkrt, &runtime) != VKRT_SUCCESS) {
        return;
    }

    uint64_t nowUs = getMicroseconds();
    SessionRenderTimer* timer = &session->runtime.renderTimer;

    if (!state->renderModeActive) {
        drawIdleRenderState(vkrt, session, timer);
        return;
    }

    float elapsedActiveSeconds = queryActiveRenderSeconds(timer, nowUs);
    drawRenderProgressSection(state, &runtime, &session->runtime.sequenceProgress, elapsedActiveSeconds, timer->completedSeconds);
    drawRenderActionsSection(vkrt, session, state, &session->runtime.sequenceProgress);
}
