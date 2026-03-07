#include "session.h"
#include "debug.h"
#include "io.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* kDefaultRenderSequenceFolder = "captures/sequence";
static const float kDefaultTimelineStartTime = 0.0f;
static const float kDefaultTimelineEndTime = 0.5f;
static const float kDefaultTimelineEmissionScale = 1.0f;

static int compareTimelineKeyframesByTime(const void* lhs, const void* rhs) {
    const SessionSceneTimelineKeyframe* a = (const SessionSceneTimelineKeyframe*)lhs;
    const SessionSceneTimelineKeyframe* b = (const SessionSceneTimelineKeyframe*)rhs;
    if (a->time < b->time) return -1;
    if (a->time > b->time) return 1;
    return 0;
}

static float clampFloatValue(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static void sessionResetTimelineDefaults(SessionSceneTimelineSettings* timeline) {
    if (!timeline) return;

    timeline->enabled = 0;
    timeline->keyframeCount = 2;
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

static int ensureMeshSlotCount(Session* session, uint32_t requiredCount) {
    if (!session || requiredCount <= session->editor.meshCount) return 1;

    char** resized = (char**)realloc(session->editor.meshNames, (size_t)requiredCount * sizeof(char*));
    if (!resized) {
        LOG_ERROR("Failed to resize mesh label list");
        return 0;
    }

    for (uint32_t index = session->editor.meshCount; index < requiredCount; index++) {
        resized[index] = NULL;
    }

    session->editor.meshNames = resized;
    session->editor.meshCount = requiredCount;
    return 1;
}

void sessionInit(Session* session) {
    if (!session) return;

    memset(session, 0, sizeof(*session));
    session->commands.meshToRemove = VKRT_INVALID_INDEX;
    session->commands.renderCommand = SESSION_RENDER_COMMAND_NONE;
    session->editor.renderConfig.width = 1920;
    session->editor.renderConfig.height = 1080;
    session->editor.renderConfig.targetSamples = 1024;
    session->editor.renderConfig.animation.minTime = kDefaultTimelineStartTime;
    session->editor.renderConfig.animation.maxTime = kDefaultTimelineEndTime;
    session->editor.renderConfig.animation.timeStep = 0.05f;
    sessionResetTimelineDefaults(&session->editor.renderConfig.animation.sceneTimeline);

    char capturesPath[PATH_MAX] = {0};
    if (resolveExistingPath("captures", capturesPath, sizeof(capturesPath)) == 0) {
        char sequencePath[PATH_MAX] = {0};
        if (snprintf(sequencePath, sizeof(sequencePath), "%s/sequence", capturesPath) < (int)sizeof(sequencePath)) {
            sessionSetRenderSequenceFolder(session, sequencePath);
            return;
        }
    }

    sessionSetRenderSequenceFolder(session, kDefaultRenderSequenceFolder);
}

void sessionDeinit(Session* session) {
    if (!session) return;

    for (uint32_t index = 0; index < session->editor.meshCount; index++) {
        free(session->editor.meshNames[index]);
    }
    free(session->editor.meshNames);
    session->editor.meshNames = NULL;
    session->editor.meshCount = 0;

    clearOwnedString(&session->commands.meshImportPath);
    clearOwnedString(&session->commands.saveImagePath);
    clearOwnedString(&session->editor.renderSequenceFolderPath);
}

int sessionSetMeshName(Session* session, const char* filePath, uint32_t meshIndex) {
    if (!session) return 0;

    if (!ensureMeshSlotCount(session, meshIndex + 1)) return 0;
    free(session->editor.meshNames[meshIndex]);
    const char* meshName = pathBasename(filePath);
    if (!meshName[0]) meshName = "(unknown)";
    session->editor.meshNames[meshIndex] = stringDuplicate(meshName);
    return session->editor.meshNames[meshIndex] != NULL;
}

void sessionRemoveMeshName(Session* session, uint32_t meshIndex) {
    if (!session || meshIndex >= session->editor.meshCount) return;

    free(session->editor.meshNames[meshIndex]);
    for (uint32_t index = meshIndex; index + 1 < session->editor.meshCount; index++) {
        session->editor.meshNames[index] = session->editor.meshNames[index + 1];
    }

    session->editor.meshCount--;
    if (session->editor.meshCount == 0) {
        free(session->editor.meshNames);
        session->editor.meshNames = NULL;
        return;
    }

    char** shrunk = (char**)realloc(session->editor.meshNames, (size_t)session->editor.meshCount * sizeof(char*));
    if (shrunk) session->editor.meshNames = shrunk;
}

const char* sessionGetMeshName(const Session* session, uint32_t meshIndex) {
    if (!session || meshIndex >= session->editor.meshCount || !session->editor.meshNames[meshIndex]) {
        return "(unknown)";
    }
    return session->editor.meshNames[meshIndex];
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
        if (!isfinite(key->time)) key->time = kDefaultTimelineStartTime;
        key->time = clampFloatValue(key->time, SESSION_SCENE_TIMELINE_TIME_MIN, SESSION_SCENE_TIMELINE_TIME_MAX);

        if (!isfinite(key->emissionScale)) key->emissionScale = 0.0f;
        key->emissionScale = clampFloatValue(key->emissionScale,
            SESSION_SCENE_TIMELINE_EMISSION_SCALE_MIN,
            SESSION_SCENE_TIMELINE_EMISSION_SCALE_MAX);

        for (int channel = 0; channel < 3; channel++) {
            if (!isfinite(key->emissionTint[channel])) key->emissionTint[channel] = 1.0f;
            key->emissionTint[channel] = clampFloatValue(key->emissionTint[channel],
                SESSION_SCENE_TIMELINE_EMISSION_TINT_MIN,
                SESSION_SCENE_TIMELINE_EMISSION_TINT_MAX);
        }
    }

    qsort(timeline->keyframes,
        timeline->keyframeCount,
        sizeof(timeline->keyframes[0]),
        compareTimelineKeyframesByTime);
}

void sessionSanitizeAnimationSettings(SessionRenderAnimationSettings* animation) {
    if (!animation) return;
    if (!isfinite(animation->minTime) || animation->minTime < 0.0f) animation->minTime = 0.0f;
    if (!isfinite(animation->maxTime) || animation->maxTime < animation->minTime) animation->maxTime = animation->minTime;
    if (!isfinite(animation->timeStep) || animation->timeStep <= 0.0f) animation->timeStep = 0.05f;
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
