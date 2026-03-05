#include "common.h"

#include "IconsFontAwesome6.h"
#include "debug.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>

static void initializeRenderConfig(Session* session) {
    if (!session) return;
    if (session->renderConfig.width == 0 || session->renderConfig.height == 0) {
        session->renderConfig.width = 1920;
        session->renderConfig.height = 1080;
    }
    sessionSanitizeAnimationSettings(&session->renderConfig.animation);
}

void inspectorDrawRenderTab(VKRT* vkrt, Session* session, const VKRT_PublicState* state, const VKRT_RuntimeSnapshot* runtime) {
    if (!vkrt || !session || !state || !runtime) return;
    initializeRenderConfig(session);

    static uint8_t sRenderTimerWasActive = 0;
    static uint64_t sRenderTimerStartUs = 0;
    static float sRenderTotalSeconds = 0.0f;

    uint8_t renderModeActive = state->renderModeActive != 0;
    uint8_t renderModeFinished = (renderModeActive && state->renderModeFinished != 0) ? 1u : 0u;
    uint64_t nowUs = getMicroseconds();

    if (renderModeActive && !sRenderTimerWasActive) {
        sRenderTimerStartUs = nowUs;
        sRenderTotalSeconds = 0.0f;
    }

    if (renderModeActive && renderModeFinished && sRenderTotalSeconds <= 0.0f) {
        if (sRenderTimerStartUs > 0 && nowUs >= sRenderTimerStartUs) {
            sRenderTotalSeconds = (float)(nowUs - sRenderTimerStartUs) / 1000000.0f;
        }
    }

    if (!renderModeActive && sRenderTimerWasActive) {
        if (sRenderTotalSeconds <= 0.0f && sRenderTimerStartUs > 0 && nowUs >= sRenderTimerStartUs) {
            sRenderTotalSeconds = (float)(nowUs - sRenderTimerStartUs) / 1000000.0f;
        }
        sRenderTimerStartUs = 0;
    }

    sRenderTimerWasActive = renderModeActive;

    SessionRenderAnimationSettings* anim = &session->renderConfig.animation;

    if (!state->renderModeActive) {
        int outputSize[2] = {(int)session->renderConfig.width, (int)session->renderConfig.height};
        int targetSamples = (int)session->renderConfig.targetSamples;
        bool animationEnabled = anim->enabled != 0;
        uint32_t frameCount = sessionComputeAnimationFrameCount(anim);
        const char* sequenceFolder = sessionGetRenderSequenceFolder(session);
        if (!sequenceFolder || !sequenceFolder[0]) sequenceFolder = "(not set)";

        const float inputWidth = queryInspectorInputWidth(220.0f, 132.0f);
        const float folderWidth = queryInspectorInputWidth(260.0f, 100.0f);

        if (ImGui_CollapsingHeader("Output", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui_Indent();
            ImGui_SetNextItemWidth(inputWidth);
            if (ImGui_DragInt2Ex(ICON_FA_CAMERA_RETRO " Output Size", outputSize, 1.0f, 1, 16384, "%d", ImGuiSliderFlags_AlwaysClamp)) {
                session->renderConfig.width = clampRenderDimension(outputSize[0]);
                session->renderConfig.height = clampRenderDimension(outputSize[1]);
            }

            ImGui_SetNextItemWidth(inputWidth);
            if (ImGui_DragIntEx(ICON_FA_IMAGES " Samples", &targetSamples, 1.0f, 0, INT_MAX, "%d", ImGuiSliderFlags_AlwaysClamp)) {
                if (targetSamples < 0) targetSamples = 0;
                session->renderConfig.targetSamples = (uint32_t)targetSamples;
            }
            tooltipOnHover("Total samples to render. Set to 0 for manual stop.");
            ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
            ImGui_Unindent();
        }

        if (ImGui_CollapsingHeader("Animation", ImGuiTreeNodeFlags_None)) {
            ImGui_Indent();
            if (ImGui_Checkbox("Enabled##render_animation_enabled", &animationEnabled)) {
                anim->enabled = animationEnabled ? 1 : 0;
                if (!animationEnabled) {
                    sessionSanitizeAnimationSettings(anim);
                }
            }
            tooltipOnHover("Render a sequence by stepping light travel time from Min to Max.");
            ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});

            float timeMin = anim->minTime;
            float timeMax = anim->maxTime;
            float timeStep = anim->timeStep;
            SessionSceneTimelineSettings* sceneTimeline = &anim->sceneTimeline;
            ImGui_BeginDisabled(!animationEnabled);

            ImGui_SetNextItemWidth(inputWidth);
            if (ImGui_DragFloatEx(ICON_FA_TIMELINE " Time Min", &timeMin, 0.01f, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
                anim->minTime = timeMin;
                sessionSanitizeAnimationSettings(anim);
                frameCount = sessionComputeAnimationFrameCount(anim);
            }
            tooltipOnHover("Sequence start time.");

            ImGui_SetNextItemWidth(inputWidth);
            if (ImGui_DragFloatEx(ICON_FA_TIMELINE " Time Max", &timeMax, 0.01f, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
                anim->maxTime = timeMax;
                sessionSanitizeAnimationSettings(anim);
                frameCount = sessionComputeAnimationFrameCount(anim);
            }
            tooltipOnHover("Sequence end time.");

            ImGui_SetNextItemWidth(inputWidth);
            if (ImGui_DragFloatEx(ICON_FA_CLOCK " Step", &timeStep, 0.005f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
                anim->timeStep = timeStep;
                sessionSanitizeAnimationSettings(anim);
                frameCount = sessionComputeAnimationFrameCount(anim);
            }
            tooltipOnHover("Time increment per sequence frame.");

            ImGui_Text(ICON_FA_IMAGES " Frames: %u", frameCount);
            ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});

            if (ImGui_Button(ICON_FA_FOLDER_OPEN " Select Folder")) {
                sessionRequestRenderSequenceFolderDialog(session);
            }

            char folderPathBuffer[256] = {0};
            snprintf(folderPathBuffer, sizeof(folderPathBuffer), "%s", sequenceFolder);
            ImGui_SetNextItemWidth(folderWidth);
            ImGui_InputTextEx("Folder", folderPathBuffer, sizeof(folderPathBuffer), ImGuiInputTextFlags_ReadOnly, NULL, NULL);
            tooltipOnHover(sequenceFolder);

            ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing * 1.25f});

            if (ImGui_CollapsingHeader("Timeline", ImGuiTreeNodeFlags_None)) {
                ImGui_Indent();
                bool timelineEnabled = sceneTimeline->enabled != 0;
                if (ImGui_Checkbox("Enabled##render_timeline_enabled", &timelineEnabled)) {
                    sceneTimeline->enabled = timelineEnabled ? 1 : 0;
                    sessionSanitizeAnimationSettings(anim);
                }
                tooltipOnHover("Step emission values at discrete points in source time.");
                ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});

                ImGui_BeginDisabled(!timelineEnabled);
                bool timelineEdited = false;
                bool timeActivelyEditing = false;

                if (ImGui_Button(ICON_FA_PLUS " Add")) {
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
                    if (ImGui_Button(ICON_FA_MINUS " Remove")) {
                        sceneTimeline->keyframeCount--;
                        timelineEdited = true;
                    }
                }

                for (uint32_t keyIndex = 0; keyIndex < sceneTimeline->keyframeCount; keyIndex++) {
                    SessionSceneTimelineKeyframe* key = &sceneTimeline->keyframes[keyIndex];
                    ImGui_PushIDInt((int)keyIndex);
                    ImGui_SeparatorText("Marker");

                    ImGui_SetNextItemWidth(inputWidth);
                    ImGui_DragFloatEx("Time", &key->time, 0.01f,
                        SESSION_SCENE_TIMELINE_TIME_MIN,
                        SESSION_SCENE_TIMELINE_TIME_MAX,
                        "%.3f",
                        ImGuiSliderFlags_AlwaysClamp);
                    timelineEdited |= ImGui_IsItemDeactivatedAfterEdit();
                    timeActivelyEditing |= ImGui_IsItemActive();

                    ImGui_SetNextItemWidth(inputWidth);
                    timelineEdited |= ImGui_DragFloatEx("Emission Scale", &key->emissionScale, 0.01f,
                        SESSION_SCENE_TIMELINE_EMISSION_SCALE_MIN,
                        SESSION_SCENE_TIMELINE_EMISSION_SCALE_MAX,
                        "%.3f", ImGuiSliderFlags_AlwaysClamp);

                    timelineEdited |= ImGui_ColorEdit3("Emission Tint", key->emissionTint, ImGuiColorEditFlags_Float);
                    ImGui_PopID();
                }

                if (timelineEdited && !timeActivelyEditing) {
                    sessionSanitizeAnimationSettings(anim);
                }
                ImGui_EndDisabled();
                ImGui_Unindent();
            }

            if (session->renderConfig.targetSamples == 0) {
                ImGui_TextDisabled("Sequence mode needs finite samples. `0` will be promoted to `1`.");
            }
            ImGui_EndDisabled();
            ImGui_Unindent();
        }

        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing * 1.5f});
        const char* startLabel = animationEnabled
            ? ICON_FA_CLAPPERBOARD " Start Sequence"
            : ICON_FA_CAMERA " Start Render";
        if (ImGui_Button(startLabel)) {
            uint32_t startSamples = session->renderConfig.targetSamples;
            if (animationEnabled && startSamples == 0) startSamples = 1;
            sessionQueueRenderStart(session,
                session->renderConfig.width,
                session->renderConfig.height,
                startSamples,
                anim);
        }
        if (sRenderTotalSeconds > 0.0f) {
            char totalText[32] = {0};
            formatTime(sRenderTotalSeconds, totalText, sizeof(totalText));
            ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
            ImGui_Text(ICON_FA_CLOCK " Last Render Time: %s", totalText);
        }
        return;
    }

    const SessionSequenceProgress* seq = &session->sequenceProgress;
    float elapsedActiveSeconds = 0.0f;
    if (sRenderTimerStartUs > 0 && nowUs >= sRenderTimerStartUs) {
        elapsedActiveSeconds = (float)(nowUs - sRenderTimerStartUs) / 1000000.0f;
    }

    if (ImGui_CollapsingHeader("Progress", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui_Indent();
        ImGui_PushStyleColorImVec4(ImGuiCol_FrameBg, kProgressBgColor);
        ImGui_PushStyleColorImVec4(ImGuiCol_PlotHistogram, kProgressFillColor);
        ImGui_PushStyleColorImVec4(ImGuiCol_PlotHistogramHovered, (ImVec4){0.40f, 0.40f, 0.40f, 1.00f});
        ImGui_PushStyleColorImVec4(ImGuiCol_Text, kProgressTextColor);
        ImGui_Text(ICON_FA_CAMERA_RETRO " Output: %ux%u", runtime->renderWidth, runtime->renderHeight);
        if (state->renderTargetSamples > 0) {
            uint64_t shownSamples = state->totalSamples;
            if (shownSamples > state->renderTargetSamples) {
                shownSamples = state->renderTargetSamples;
            }
            float progress = (float)shownSamples / (float)state->renderTargetSamples;
            if (progress < 0.0f) progress = 0.0f;
            if (progress > 1.0f) progress = 1.0f;
            char overlay[96] = {0};
            snprintf(overlay, sizeof(overlay), "%llu / %u Samples",
                (unsigned long long)shownSamples, state->renderTargetSamples);
            ImGui_ProgressBar(progress, (ImVec2){-1.0f, 0.0f}, overlay);
        } else {
            char overlay[96] = {0};
            snprintf(overlay, sizeof(overlay), "%llu Samples",
                (unsigned long long)state->totalSamples);
            ImGui_ProgressBar(0.0f, (ImVec2){-1.0f, 0.0f}, overlay);
        }
        ImGui_Text("%s", state->renderModeFinished ? "Status: Complete" : "Status: Rendering");

        if (seq->active) {
            float sequenceProgress = 0.0f;
            if (seq->frameCount > 0u) {
                uint32_t frameShown = seq->frameIndex;
                if (frameShown > seq->frameCount) frameShown = seq->frameCount;
                sequenceProgress = (float)frameShown / (float)seq->frameCount;
            }
            if (sequenceProgress < 0.0f) sequenceProgress = 0.0f;
            if (sequenceProgress > 1.0f) sequenceProgress = 1.0f;

            char sequenceOverlay[96] = {0};
            snprintf(sequenceOverlay, sizeof(sequenceOverlay), "Sequence %u / %u",
                seq->frameIndex, seq->frameCount);
            ImGui_ProgressBar(sequenceProgress, (ImVec2){-1.0f, 0.0f}, sequenceOverlay);

            if (!state->renderModeFinished) {
                if (seq->hasEstimatedRemaining) {
                    char etaText[32] = {0};
                    formatTime(fmaxf(seq->estimatedRemainingSeconds, 0.0f), etaText, sizeof(etaText));
                    ImGui_Text(ICON_FA_CLOCK " ETA: %s", etaText);
                } else {
                    ImGui_Text(ICON_FA_CLOCK " ETA: --");
                }
            }
        } else if (!state->renderModeFinished) {
            if (state->renderTargetSamples > 0 && state->totalSamples > 0 && elapsedActiveSeconds > 0.0f) {
                float samplesPerSecond = (float)state->totalSamples / elapsedActiveSeconds;
                uint64_t remainingSamples = 0;
                if (state->renderTargetSamples > state->totalSamples) {
                    remainingSamples = state->renderTargetSamples - state->totalSamples;
                }

                if (samplesPerSecond > 0.0f) {
                    float etaSeconds = (float)remainingSamples / samplesPerSecond;
                    char etaText[32] = {0};
                    formatTime(fmaxf(etaSeconds, 0.0f), etaText, sizeof(etaText));
                    ImGui_Text(ICON_FA_CLOCK " ETA: %s", etaText);
                } else {
                    ImGui_Text(ICON_FA_CLOCK " ETA: --");
                }
            } else {
                ImGui_Text(ICON_FA_CLOCK " ETA: --");
            }
        }

        if (state->renderModeFinished) {
            float elapsedDoneSeconds = sRenderTotalSeconds > 0.0f ? sRenderTotalSeconds : elapsedActiveSeconds;
            char elapsedText[32] = {0};
            formatTime(fmaxf(elapsedDoneSeconds, 0.0f), elapsedText, sizeof(elapsedText));
            ImGui_Text(ICON_FA_CLOCK " Elapsed: %s", elapsedText);
        }
        ImGui_PopStyleColorEx(4);
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
    }

    if (ImGui_CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui_Indent();
        if (!state->renderModeFinished) {
            if (seq->active) {
                if (ImGui_Button(ICON_FA_STOP " Stop Sequence")) {
                    sessionQueueRenderStop(session);
                }
            } else if (ImGui_Button(ICON_FA_STOP " Stop Render")) {
                VKRT_Result result = VKRT_stopRenderSampling(vkrt);
                if (result != VKRT_SUCCESS) {
                    LOG_ERROR("Stopping render sampling failed (%d)", (int)result);
                }
            }
        } else {
            if (!seq->active) {
                if (ImGui_Button(ICON_FA_FLOPPY_DISK " Save Image")) {
                    sessionRequestRenderSaveDialog(session);
                }
                ImGui_SameLine();
            }
            if (ImGui_Button(ICON_FA_ARROW_RIGHT_FROM_BRACKET " Exit Render")) {
                sessionQueueRenderStop(session);
            }
        }
        ImGui_Dummy((ImVec2){0.0f, kInspectorControlSpacing});
        ImGui_Unindent();
    }
}
