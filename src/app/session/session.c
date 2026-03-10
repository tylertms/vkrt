#include "session.h"
#include "debug.h"
#include "io.h"
#include "numeric.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* kDefaultRenderSequenceFolder = "captures/sequence";
static const float kDefaultTimelineStartTime = 0.0f;
static const float kDefaultTimelineEndTime = 0.5f;
static const float kDefaultTimelineEmissionScale = 1.0f;
static const float kDefaultAnimationStep = 0.05f;
static const uint32_t kDefaultTimelineKeyframeCount = 2u;
static const uint32_t kDefaultRenderTargetSamples = 1024u;

static void sessionResetTimelineDefaults(SessionSceneTimelineSettings* timeline) {
    if (!timeline) return;

    timeline->enabled = 0;
    timeline->keyframeCount = kDefaultTimelineKeyframeCount;
    timeline->keyframes[0] = (SessionSceneTimelineKeyframe){
        .time = kDefaultTimelineStartTime,
        .emissionScale = kDefaultTimelineEmissionScale,
        .emissionTint = {1.0f, 1.0f, 1.0f},
    };
    timeline->keyframes[1] = (SessionSceneTimelineKeyframe){
        .time = kDefaultTimelineEndTime,
        .emissionScale = kDefaultTimelineEmissionScale,
        .emissionTint = {1.0f, 1.0f, 1.0f},
    };
}

static void clearOwnedString(char** value) {
    if (!value || !*value) return;
    free(*value);
    *value = NULL;
}

void sessionInit(Session* session) {
    if (!session) return;

    memset(session, 0, sizeof(*session));
    session->commands.meshToRemove = VKRT_INVALID_INDEX;
    session->commands.renderCommand = SESSION_RENDER_COMMAND_NONE;
    session->editor.renderConfig.targetSamples = kDefaultRenderTargetSamples;
    session->editor.renderConfig.animation.minTime = kDefaultTimelineStartTime;
    session->editor.renderConfig.animation.maxTime = kDefaultTimelineEndTime;
    session->editor.renderConfig.animation.timeStep = kDefaultAnimationStep;
    sessionResetTimelineDefaults(&session->editor.renderConfig.animation.sceneTimeline);

    char capturesPath[VKRT_PATH_MAX];
    if (resolveExistingPath("captures", capturesPath, sizeof(capturesPath)) == 0) {
        char sequencePath[VKRT_PATH_MAX];
        if (snprintf(sequencePath, sizeof(sequencePath), "%s/sequence", capturesPath) < (int)sizeof(sequencePath)) {
            sessionSetRenderSequenceFolder(session, sequencePath);
            return;
        }
    }

    sessionSetRenderSequenceFolder(session, kDefaultRenderSequenceFolder);
}

void sessionDeinit(Session* session) {
    if (!session) return;

    clearOwnedString(&session->commands.meshImportPath);
    clearOwnedString(&session->commands.saveImagePath);
    clearOwnedString(&session->editor.renderSequenceFolderPath);
}

void sessionRequestMeshImportDialog(Session* session) {
    if (!session) return;
    session->editor.requestMeshImportDialog = 1;
}

void sessionRequestRenderSaveDialog(Session* session) {
    if (!session) return;
    session->editor.requestRenderSaveDialog = 1;
}

void sessionRequestRenderSequenceFolderDialog(Session* session) {
    if (!session) return;
    session->editor.requestRenderSequenceFolderDialog = 1;
}

int sessionTakeMeshImportDialogRequest(Session* session) {
    if (!session || !session->editor.requestMeshImportDialog) return 0;
    session->editor.requestMeshImportDialog = 0;
    return 1;
}

int sessionTakeRenderSaveDialogRequest(Session* session) {
    if (!session || !session->editor.requestRenderSaveDialog) return 0;
    session->editor.requestRenderSaveDialog = 0;
    return 1;
}

int sessionTakeRenderSequenceFolderDialogRequest(Session* session) {
    if (!session || !session->editor.requestRenderSequenceFolderDialog) return 0;
    session->editor.requestRenderSequenceFolderDialog = 0;
    return 1;
}

void sessionQueueMeshImport(Session* session, const char* path) {
    if (!session) return;

    clearOwnedString(&session->commands.meshImportPath);
    if (!path || !path[0]) return;
    session->commands.meshImportPath = stringDuplicate(path);
}

void sessionQueueMeshRemoval(Session* session, uint32_t meshIndex) {
    if (!session) return;
    session->commands.meshToRemove = meshIndex;
}

int sessionTakeMeshImport(Session* session, char** outPath) {
    if (!session || !session->commands.meshImportPath) return 0;

    char* path = session->commands.meshImportPath;
    session->commands.meshImportPath = NULL;
    if (outPath) {
        *outPath = path;
    } else {
        free(path);
    }
    return 1;
}

int sessionTakeMeshRemoval(Session* session, uint32_t* outMeshIndex) {
    if (!session || session->commands.meshToRemove == VKRT_INVALID_INDEX) return 0;

    if (outMeshIndex) *outMeshIndex = session->commands.meshToRemove;
    session->commands.meshToRemove = VKRT_INVALID_INDEX;
    return 1;
}

void sessionQueueRenderSave(Session* session, const char* path) {
    if (!session) return;

    clearOwnedString(&session->commands.saveImagePath);
    if (!path || !path[0]) return;

    session->commands.saveImagePath = stringDuplicate(path);
}

void sessionSetRenderSequenceFolder(Session* session, const char* path) {
    if (!session) return;

    clearOwnedString(&session->editor.renderSequenceFolderPath);
    if (!path || !path[0]) return;
    session->editor.renderSequenceFolderPath = stringDuplicate(path);
}

const char* sessionGetRenderSequenceFolder(const Session* session) {
    if (!session || !session->editor.renderSequenceFolderPath || !session->editor.renderSequenceFolderPath[0]) {
        return "";
    }
    return session->editor.renderSequenceFolderPath;
}

int sessionTakeRenderSave(Session* session, char** outPath) {
    if (!session || !session->commands.saveImagePath) return 0;

    char* path = session->commands.saveImagePath;
    session->commands.saveImagePath = NULL;
    if (outPath) {
        *outPath = path;
    } else {
        free(path);
    }
    return 1;
}

void sessionQueueRenderStart(Session* session, uint32_t width, uint32_t height, uint32_t targetSamples, const SessionRenderAnimationSettings* animation) {
    if (!session) return;

    if (width == 0) width = 1;
    if (height == 0) height = 1;

    SessionRenderAnimationSettings animationSettings = {0};
    if (animation) {
        animationSettings = *animation;
    }
    sessionSanitizeAnimationSettings(&animationSettings);

    session->commands.renderCommand = SESSION_RENDER_COMMAND_START;
    session->commands.pendingRenderJob.width = width;
    session->commands.pendingRenderJob.height = height;
    session->commands.pendingRenderJob.targetSamples = targetSamples;
    session->commands.pendingRenderJob.animation = animationSettings;
}

void sessionQueueRenderStop(Session* session) {
    if (!session) return;
    session->commands.renderCommand = SESSION_RENDER_COMMAND_STOP;
}

int sessionTakeRenderCommand(Session* session, SessionRenderCommand* outCommand, SessionRenderSettings* outSettings) {
    if (!session || session->commands.renderCommand == SESSION_RENDER_COMMAND_NONE) return 0;

    SessionRenderCommand command = session->commands.renderCommand;
    SessionRenderSettings settings = session->commands.pendingRenderJob;
    session->commands.renderCommand = SESSION_RENDER_COMMAND_NONE;

    if (outCommand) *outCommand = command;
    if (outSettings) *outSettings = settings;
    return 1;
}

static void sessionSanitizeTimelineSettings(SessionSceneTimelineSettings* timeline) {
    if (!timeline) return;

    if (timeline->keyframeCount == 0 || timeline->keyframeCount > SESSION_SCENE_TIMELINE_KEYFRAME_CAPACITY) {
        sessionResetTimelineDefaults(timeline);
    }

    for (uint32_t keyIndex = 0; keyIndex < timeline->keyframeCount; keyIndex++) {
        SessionSceneTimelineKeyframe* key = &timeline->keyframes[keyIndex];
        key->time = vkrtFiniteClampf(key->time, kDefaultTimelineStartTime, SESSION_SCENE_TIMELINE_TIME_MIN, SESSION_SCENE_TIMELINE_TIME_MAX);

        key->emissionScale = vkrtFiniteClampf(key->emissionScale, 0.0f, SESSION_SCENE_TIMELINE_EMISSION_SCALE_MIN, SESSION_SCENE_TIMELINE_EMISSION_SCALE_MAX);

        for (int channel = 0; channel < 3; channel++) {
            key->emissionTint[channel] = vkrtFiniteClampf(key->emissionTint[channel], 1.0f, SESSION_SCENE_TIMELINE_EMISSION_TINT_MIN, SESSION_SCENE_TIMELINE_EMISSION_TINT_MAX);
        }
    }

    qsort(timeline->keyframes,
        timeline->keyframeCount,
        sizeof(timeline->keyframes[0]),
        vkrtCompareSceneTimelineKeyframesByTime);
}

void sessionSanitizeAnimationSettings(SessionRenderAnimationSettings* animation) {
    if (!animation) return;
    animation->minTime = vkrtFiniteClampf(animation->minTime, 0.0f, 0.0f, INFINITY);
    animation->maxTime = vkrtFiniteOrf(animation->maxTime, animation->minTime);
    if (animation->maxTime < animation->minTime) animation->maxTime = animation->minTime;
    animation->timeStep = vkrtFiniteOrf(animation->timeStep, kDefaultAnimationStep);
    if (animation->timeStep <= 0.0f) animation->timeStep = kDefaultAnimationStep;
    sessionSanitizeTimelineSettings(&animation->sceneTimeline);
}

uint32_t sessionComputeAnimationFrameCount(const SessionRenderAnimationSettings* animation) {
    if (!animation) return 0;
    if (!isfinite(animation->minTime) || !isfinite(animation->maxTime) || !isfinite(animation->timeStep)) return 0;
    if (animation->timeStep <= 0.0f || animation->maxTime < animation->minTime) return 0;

    double span = (double)animation->maxTime - (double)animation->minTime;
    double steps = floor(span / (double)animation->timeStep + 1e-6);
    if (!isfinite(steps) || steps < 0.0) return 0;
    double count = steps + 1.0;
    if (count > (double)UINT32_MAX) return UINT32_MAX;
    return (uint32_t)count;
}
