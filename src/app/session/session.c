#include "session.h"
#include "debug.h"

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

static float clampFloat(float value, float minValue, float maxValue) {
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

static const char* fileBasename(const char* path) {
    if (!path || !path[0]) return "(unknown)";

    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* base = path;

    if (slash && backslash) {
        base = slash > backslash ? slash + 1 : backslash + 1;
    } else if (slash) {
        base = slash + 1;
    } else if (backslash) {
        base = backslash + 1;
    }

    return base;
}

static char* stringDuplicate(const char* value) {
    size_t length = strlen(value);
    char* copy = (char*)malloc(length + 1);
    if (!copy) return NULL;

    memcpy(copy, value, length + 1);
    return copy;
}

static void clearOwnedString(char** value) {
    if (!value || !*value) return;
    free(*value);
    *value = NULL;
}

static void ensureMeshSlotCount(Session* session, uint32_t requiredCount) {
    if (!session || requiredCount <= session->meshCount) return;

    char** resized = (char**)realloc(session->meshNames, (size_t)requiredCount * sizeof(char*));
    if (!resized) {
        LOG_ERROR("Failed to resize mesh label list");
        exit(EXIT_FAILURE);
    }

    for (uint32_t index = session->meshCount; index < requiredCount; index++) {
        resized[index] = NULL;
    }

    session->meshNames = resized;
    session->meshCount = requiredCount;
}

void sessionInit(Session* session) {
    if (!session) return;

    memset(session, 0, sizeof(*session));
    session->meshToRemove = UINT32_MAX;
    session->renderCommand = SESSION_RENDER_COMMAND_NONE;
    session->renderConfig.targetSamples = 1024;
    session->renderConfig.animation.minTime = kDefaultTimelineStartTime;
    session->renderConfig.animation.maxTime = kDefaultTimelineEndTime;
    session->renderConfig.animation.timeStep = 0.05f;
    sessionResetTimelineDefaults(&session->renderConfig.animation.sceneTimeline);
    sessionSetRenderSequenceFolder(session, kDefaultRenderSequenceFolder);
}

void sessionDeinit(Session* session) {
    if (!session) return;

    for (uint32_t index = 0; index < session->meshCount; index++) {
        free(session->meshNames[index]);
    }
    free(session->meshNames);
    session->meshNames = NULL;
    session->meshCount = 0;

    clearOwnedString(&session->meshImportPath);
    clearOwnedString(&session->saveImagePath);
    clearOwnedString(&session->renderSequenceFolderPath);
}

void sessionSetMeshName(Session* session, const char* filePath, uint32_t meshIndex) {
    if (!session) return;

    ensureMeshSlotCount(session, meshIndex + 1);
    free(session->meshNames[meshIndex]);
    session->meshNames[meshIndex] = stringDuplicate(fileBasename(filePath));
}

void sessionRemoveMeshName(Session* session, uint32_t meshIndex) {
    if (!session || meshIndex >= session->meshCount) return;

    free(session->meshNames[meshIndex]);
    for (uint32_t index = meshIndex; index + 1 < session->meshCount; index++) {
        session->meshNames[index] = session->meshNames[index + 1];
    }

    session->meshCount--;
    if (session->meshCount == 0) {
        free(session->meshNames);
        session->meshNames = NULL;
        return;
    }

    char** shrunk = (char**)realloc(session->meshNames, (size_t)session->meshCount * sizeof(char*));
    if (shrunk) session->meshNames = shrunk;
}

const char* sessionGetMeshName(const Session* session, uint32_t meshIndex) {
    if (!session || meshIndex >= session->meshCount || !session->meshNames[meshIndex]) {
        return "(unknown)";
    }
    return session->meshNames[meshIndex];
}

void sessionRequestMeshImportDialog(Session* session) {
    if (!session) return;
    session->requestMeshImportDialog = 1;
}

void sessionRequestRenderSaveDialog(Session* session) {
    if (!session) return;
    session->requestRenderSaveDialog = 1;
}

void sessionRequestRenderSequenceFolderDialog(Session* session) {
    if (!session) return;
    session->requestRenderSequenceFolderDialog = 1;
}

int sessionTakeMeshImportDialogRequest(Session* session) {
    if (!session || !session->requestMeshImportDialog) return 0;
    session->requestMeshImportDialog = 0;
    return 1;
}

int sessionTakeRenderSaveDialogRequest(Session* session) {
    if (!session || !session->requestRenderSaveDialog) return 0;
    session->requestRenderSaveDialog = 0;
    return 1;
}

int sessionTakeRenderSequenceFolderDialogRequest(Session* session) {
    if (!session || !session->requestRenderSequenceFolderDialog) return 0;
    session->requestRenderSequenceFolderDialog = 0;
    return 1;
}

void sessionQueueMeshImport(Session* session, const char* path) {
    if (!session) return;

    clearOwnedString(&session->meshImportPath);
    if (!path || !path[0]) return;
    session->meshImportPath = stringDuplicate(path);
}

void sessionQueueMeshRemoval(Session* session, uint32_t meshIndex) {
    if (!session) return;
    session->meshToRemove = meshIndex;
}

int sessionTakeMeshImport(Session* session, char** outPath) {
    if (!session || !session->meshImportPath) return 0;

    char* path = session->meshImportPath;
    session->meshImportPath = NULL;
    if (outPath) {
        *outPath = path;
    } else {
        free(path);
    }
    return 1;
}

int sessionTakeMeshRemoval(Session* session, uint32_t* outMeshIndex) {
    if (!session || session->meshToRemove == UINT32_MAX) return 0;

    if (outMeshIndex) *outMeshIndex = session->meshToRemove;
    session->meshToRemove = UINT32_MAX;
    return 1;
}

void sessionQueueRenderSave(Session* session, const char* path) {
    if (!session) return;

    clearOwnedString(&session->saveImagePath);
    if (!path || !path[0]) return;

    session->saveImagePath = stringDuplicate(path);
}

void sessionSetRenderSequenceFolder(Session* session, const char* path) {
    if (!session) return;

    clearOwnedString(&session->renderSequenceFolderPath);
    if (!path || !path[0]) return;
    session->renderSequenceFolderPath = stringDuplicate(path);
}

const char* sessionGetRenderSequenceFolder(const Session* session) {
    if (!session || !session->renderSequenceFolderPath || !session->renderSequenceFolderPath[0]) {
        return "";
    }
    return session->renderSequenceFolderPath;
}

int sessionTakeRenderSave(Session* session, char** outPath) {
    if (!session || !session->saveImagePath) return 0;

    char* path = session->saveImagePath;
    session->saveImagePath = NULL;
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

    session->renderCommand = SESSION_RENDER_COMMAND_START;
    session->pendingRenderJob.width = width;
    session->pendingRenderJob.height = height;
    session->pendingRenderJob.targetSamples = targetSamples;
    session->pendingRenderJob.animation = animationSettings;
}

void sessionQueueRenderStop(Session* session) {
    if (!session) return;
    session->renderCommand = SESSION_RENDER_COMMAND_STOP;
}

int sessionTakeRenderCommand(Session* session, SessionRenderCommand* outCommand, SessionRenderSettings* outSettings) {
    if (!session || session->renderCommand == SESSION_RENDER_COMMAND_NONE) return 0;

    SessionRenderCommand command = session->renderCommand;
    SessionRenderSettings settings = session->pendingRenderJob;
    session->renderCommand = SESSION_RENDER_COMMAND_NONE;

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
        key->time = clampFloat(key->time, SESSION_SCENE_TIMELINE_TIME_MIN, SESSION_SCENE_TIMELINE_TIME_MAX);

        if (!isfinite(key->emissionScale)) key->emissionScale = 0.0f;
        key->emissionScale = clampFloat(key->emissionScale,
            SESSION_SCENE_TIMELINE_EMISSION_SCALE_MIN,
            SESSION_SCENE_TIMELINE_EMISSION_SCALE_MAX);

        for (int channel = 0; channel < 3; channel++) {
            if (!isfinite(key->emissionTint[channel])) key->emissionTint[channel] = 1.0f;
            key->emissionTint[channel] = clampFloat(key->emissionTint[channel],
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
