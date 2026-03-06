#include "sequencer.h"

#include "debug.h"
#include "io.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define VKRT_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define VKRT_MKDIR(path) mkdir(path, 0755)
#endif

static const float kRenderSequenceMinStep = 0.0001f;

static void clearSequencerState(RenderSequencer* sequencer) {
    if (!sequencer) return;
    memset(sequencer, 0, sizeof(*sequencer));
}

static void disableTimeRange(VKRT* vkrt) {
    if (!vkrt) return;
    const VKRT_PublicState* state = VKRT_getPublicState(vkrt);
    if (state && state->timeBase >= 0.0f) {
        VKRT_Result result = VKRT_setTimeRange(vkrt, -1.0f, -1.0f);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Disabling time range failed (%d)", (int)result);
        }
    }
}

static void disableTimeline(VKRT* vkrt) {
    if (!vkrt) return;
    VKRT_Result result = VKRT_setSceneTimeline(vkrt, NULL);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Disabling scene timeline failed (%d)", (int)result);
    }
}

static void stopSequence(RenderSequencer* sequencer, VKRT* vkrt, Session* session) {
    if (!sequencer || !vkrt || !session) return;

    memset(&session->runtime.sequenceProgress, 0, sizeof(session->runtime.sequenceProgress));
    const VKRT_PublicState* state = VKRT_getPublicState(vkrt);
    if (state && state->renderModeActive) {
        VKRT_Result result = VKRT_stopRender(vkrt);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Stopping render mode failed (%d)", (int)result);
        }
    }
    disableTimeRange(vkrt);
    disableTimeline(vkrt);
    clearSequencerState(sequencer);
}

static void completeSequenceWithPause(RenderSequencer* sequencer, Session* session) {
    if (!sequencer || !session) return;
    memset(&session->runtime.sequenceProgress, 0, sizeof(session->runtime.sequenceProgress));
    clearSequencerState(sequencer);
}

static int createDirectoryIfMissing(const char* path) {
    if (!path || !path[0]) return -1;
    if (VKRT_MKDIR(path) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

static int ensureDirectoryPath(const char* path) {
    if (!path || !path[0]) return -1;

    char buffer[RENDER_SEQUENCE_PATH_CAPACITY] = {0};
    size_t length = strlen(path);
    if (length >= sizeof(buffer)) return -1;

    memcpy(buffer, path, length + 1);
    pathTrimTrailingSeparators(buffer);
    length = strlen(buffer);

    if (length == 0) return -1;
    if (length == 2 && buffer[1] == ':') return 0;

    for (size_t i = 0; i < length; i++) {
        char ch = buffer[i];
        if (ch != '/' && ch != '\\') continue;
        if (i == 0) continue;
        if (i == 2 && buffer[1] == ':') continue;
        if (buffer[i - 1] == '/' || buffer[i - 1] == '\\') continue;

        buffer[i] = '\0';
        if (createDirectoryIfMissing(buffer) != 0) return -1;
        buffer[i] = ch;
    }

    return createDirectoryIfMissing(buffer);
}

static float queryFrameTime(const RenderSequencer* sequencer, uint32_t frameIndex) {
    float t = sequencer->minTime + sequencer->step * (float)frameIndex;
    if (frameIndex + 1 >= sequencer->frameCount || t > sequencer->maxTime) {
        t = sequencer->maxTime;
    }
    return t;
}

static int applyFrameTimeWindow(VKRT* vkrt, const RenderSequencer* sequencer, float frameTime) {
    if (!vkrt || !sequencer) return 0;
    VKRT_Result result = VKRT_setTimeRange(vkrt, sequencer->minTime, frameTime);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Applying sequence time window failed (%d)", (int)result);
        return 0;
    }
    return 1;
}

static int buildFramePath(char* outPath, size_t outPathSize, const RenderSequencer* sequencer) {
    if (!outPath || outPathSize == 0 || !sequencer->outputFolder[0]) return -1;

    size_t folderLength = strlen(sequencer->outputFolder);
    const char* separator = "";
    if (folderLength > 0) {
        char tail = sequencer->outputFolder[folderLength - 1];
        if (tail != '/' && tail != '\\') separator = "/";
    }

    int written = snprintf(outPath, outPathSize, "%s%s%04u.png",
        sequencer->outputFolder, separator, sequencer->frameIndex);
    if (written <= 0 || (size_t)written >= outPathSize) return -1;
    return 0;
}

static void updateSessionProgress(
    Session* session,
    const RenderSequencer* sequencer,
    float currentTime,
    float estimatedRemainingSeconds,
    uint8_t hasEstimatedRemaining
) {
    session->runtime.sequenceProgress.active = sequencer->active;
    session->runtime.sequenceProgress.frameIndex = sequencer->frameIndex + 1;
    session->runtime.sequenceProgress.frameCount = sequencer->frameCount;
    session->runtime.sequenceProgress.currentTime = currentTime;
    session->runtime.sequenceProgress.hasEstimatedRemaining = hasEstimatedRemaining ? 1u : 0u;
    session->runtime.sequenceProgress.estimatedRemainingSeconds = estimatedRemainingSeconds;
}

static void noteCompletedFrameTime(RenderSequencer* sequencer, uint64_t frameEndTimeUs) {
    if (!sequencer || sequencer->frameStartTimeUs == 0 || frameEndTimeUs <= sequencer->frameStartTimeUs) return;

    float seconds = (float)(frameEndTimeUs - sequencer->frameStartTimeUs) / 1000000.0f;
    if (seconds <= 0.0f) return;

    sequencer->timedFrameCount++;
    if (sequencer->recentFrameCount < RENDER_SEQUENCE_ETA_WINDOW) {
        uint32_t insertIndex = sequencer->recentFrameCount;
        sequencer->recentFrameSeconds[insertIndex] = seconds;
        sequencer->recentFrameSumSeconds += seconds;
        sequencer->recentFrameCount++;
        sequencer->recentFrameWriteIndex = sequencer->recentFrameCount % RENDER_SEQUENCE_ETA_WINDOW;
    } else {
        uint32_t insertIndex = sequencer->recentFrameWriteIndex;
        sequencer->recentFrameSumSeconds -= sequencer->recentFrameSeconds[insertIndex];
        sequencer->recentFrameSeconds[insertIndex] = seconds;
        sequencer->recentFrameSumSeconds += seconds;
        sequencer->recentFrameWriteIndex = (insertIndex + 1u) % RENDER_SEQUENCE_ETA_WINDOW;
    }

    if (sequencer->recentFrameCount > 0u) {
        sequencer->averageFrameSeconds =
            sequencer->recentFrameSumSeconds / (float)sequencer->recentFrameCount;
    }
}

static int startSequenceRender(VKRT* vkrt, const RenderSequencer* sequencer) {
    return VKRT_startRender(vkrt,
        sequencer->renderSettings.width,
        sequencer->renderSettings.height,
        sequencer->renderSettings.targetSamples) == VKRT_SUCCESS ? 0 : -1;
}

static int applyTimelineTrack(VKRT* vkrt, const SessionSceneTimelineSettings* timeline) {
    if (!vkrt) return 0;

    if (!timeline || !timeline->enabled || timeline->keyframeCount == 0) {
        VKRT_Result result = VKRT_setSceneTimeline(vkrt, NULL);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Applying disabled timeline track failed (%d)", (int)result);
            return 0;
        }
        return 1;
    }

    VKRT_SceneTimelineSettings sanitizedTimeline = *timeline;
    if (sanitizedTimeline.keyframeCount > VKRT_SCENE_TIMELINE_MAX_KEYFRAMES) {
        sanitizedTimeline.keyframeCount = VKRT_SCENE_TIMELINE_MAX_KEYFRAMES;
    }

    VKRT_Result result = VKRT_setSceneTimeline(vkrt, &sanitizedTimeline);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Applying timeline track failed (%d)", (int)result);
        return 0;
    }
    return 1;
}

static int beginSequence(VKRT* vkrt, Session* session, const SessionRenderSettings* settings, RenderSequencer* sequencer) {
    if (!vkrt || !session || !settings || !sequencer) return -1;

    clearSequencerState(sequencer);
    memset(&session->runtime.sequenceProgress, 0, sizeof(session->runtime.sequenceProgress));
    disableTimeRange(vkrt);
    disableTimeline(vkrt);

    const char* folder = sessionGetRenderSequenceFolder(session);
    if (!folder || !folder[0]) {
        LOG_ERROR("Render sequence requires an output folder");
        return -1;
    }

    if (snprintf(sequencer->outputFolder, sizeof(sequencer->outputFolder), "%s", folder) >= (int)sizeof(sequencer->outputFolder)) {
        LOG_ERROR("Render sequence output folder path is too long");
        return -1;
    }

    if (ensureDirectoryPath(sequencer->outputFolder) != 0) {
        LOG_ERROR("Failed to create render sequence output folder: %s", sequencer->outputFolder);
        return -1;
    }

    SessionRenderSettings sanitizedSettings = *settings;
    sessionSanitizeAnimationSettings(&sanitizedSettings.animation);

    float minTime = sanitizedSettings.animation.minTime;
    float maxTime = sanitizedSettings.animation.maxTime;
    float step = sanitizedSettings.animation.timeStep;
    if (!isfinite(minTime) || minTime < 0.0f) minTime = 0.0f;
    if (!isfinite(maxTime) || maxTime < minTime) maxTime = minTime;
    if (!isfinite(step) || step < kRenderSequenceMinStep) step = 0.05f;

    uint32_t frameCount = sessionComputeAnimationFrameCount(&sanitizedSettings.animation);
    if (frameCount == 0) {
        LOG_ERROR("Render sequence produced an invalid frame count");
        return -1;
    }

    sequencer->active = 1;
    sequencer->renderSettings = sanitizedSettings;
    sequencer->frameIndex = 0;
    sequencer->frameCount = frameCount;
    sequencer->minTime = minTime;
    sequencer->maxTime = maxTime;
    sequencer->step = step;
    sequencer->frameStartTimeUs = getMicroseconds();
    sequencer->timedFrameCount = 0;
    sequencer->averageFrameSeconds = 0.0f;
    sequencer->recentFrameCount = 0;
    sequencer->recentFrameWriteIndex = 0;
    sequencer->recentFrameSumSeconds = 0.0f;

    if (!applyTimelineTrack(vkrt, &sanitizedSettings.animation.sceneTimeline)) {
        clearSequencerState(sequencer);
        memset(&session->runtime.sequenceProgress, 0, sizeof(session->runtime.sequenceProgress));
        disableTimeRange(vkrt);
        disableTimeline(vkrt);
        return -1;
    }

    float currentBaseTime = queryFrameTime(sequencer, 0);
    updateSessionProgress(session, sequencer, currentBaseTime, 0.0f, 0);
    if (!applyFrameTimeWindow(vkrt, sequencer, currentBaseTime)) {
        clearSequencerState(sequencer);
        memset(&session->runtime.sequenceProgress, 0, sizeof(session->runtime.sequenceProgress));
        disableTimeRange(vkrt);
        disableTimeline(vkrt);
        return -1;
    }

    if (startSequenceRender(vkrt, sequencer) != 0) {
        LOG_ERROR("Failed to start render sequence");
        clearSequencerState(sequencer);
        memset(&session->runtime.sequenceProgress, 0, sizeof(session->runtime.sequenceProgress));
        disableTimeRange(vkrt);
        disableTimeline(vkrt);
        return -1;
    }

    LOG_INFO("Render sequence started. Frames: %u, Time: %.3f -> %.3f (step %.3f), Keyframes: %s, Folder: %s",
        frameCount,
        minTime,
        maxTime,
        step,
        sanitizedSettings.animation.sceneTimeline.enabled ? "On" : "Off",
        sequencer->outputFolder);
    return 0;
}

void renderSequencerHandleCommands(RenderSequencer* sequencer, VKRT* vkrt, Session* session) {
    if (!sequencer || !vkrt || !session) return;

    SessionRenderCommand command = SESSION_RENDER_COMMAND_NONE;
    SessionRenderSettings settings = {0};
    if (!sessionTakeRenderCommand(session, &command, &settings)) return;

    if (command == SESSION_RENDER_COMMAND_START) {
        if (settings.animation.enabled) {
            if (beginSequence(vkrt, session, &settings, sequencer) != 0) {
                LOG_ERROR("Failed to start render sequence command");
            }
        } else {
            clearSequencerState(sequencer);
            memset(&session->runtime.sequenceProgress, 0, sizeof(session->runtime.sequenceProgress));
            disableTimeRange(vkrt);
            disableTimeline(vkrt);
            VKRT_Result result = VKRT_startRender(vkrt, settings.width, settings.height, settings.targetSamples);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Starting render command failed (%d)", (int)result);
                sessionQueueRenderStop(session);
            }
        }
    } else if (command == SESSION_RENDER_COMMAND_STOP) {
        stopSequence(sequencer, vkrt, session);
    }
}

void renderSequencerUpdate(RenderSequencer* sequencer, VKRT* vkrt, Session* session) {
    if (!sequencer || !vkrt || !session || !sequencer->active) return;

    const VKRT_PublicState* state = VKRT_getPublicState(vkrt);
    if (!state) {
        stopSequence(sequencer, vkrt, session);
        return;
    }

    if (!state->renderModeActive) {
        stopSequence(sequencer, vkrt, session);
        return;
    }

    if (!state->renderModeFinished) return;

    char framePath[RENDER_SEQUENCE_PATH_CAPACITY + 96] = {0};
    if (buildFramePath(framePath, sizeof(framePath), sequencer) != 0 ||
        VKRT_saveRenderPNG(vkrt, framePath) != VKRT_SUCCESS) {
        LOG_ERROR("Failed to save render sequence frame %u", sequencer->frameIndex);
        stopSequence(sequencer, vkrt, session);
        return;
    }

    uint64_t frameEndTimeUs = getMicroseconds();
    noteCompletedFrameTime(sequencer, frameEndTimeUs);

    uint32_t nextFrame = sequencer->frameIndex + 1;
    if (nextFrame >= sequencer->frameCount) {
        LOG_INFO("Render sequence complete. Frames saved: %u", sequencer->frameCount);
        completeSequenceWithPause(sequencer, session);
        return;
    }

    sequencer->frameIndex = nextFrame;
    float nextBaseTime = queryFrameTime(sequencer, nextFrame);
    uint32_t remainingFrames = sequencer->frameCount - nextFrame;
    float estimatedRemainingSeconds = 0.0f;
    uint8_t hasEstimatedRemaining = 0;
    if (sequencer->recentFrameCount >= 2 && remainingFrames > 0) {
        estimatedRemainingSeconds = sequencer->averageFrameSeconds * (float)remainingFrames;
        hasEstimatedRemaining = 1;
    }

    updateSessionProgress(
        session,
        sequencer,
        nextBaseTime,
        estimatedRemainingSeconds,
        hasEstimatedRemaining);
    if (!applyFrameTimeWindow(vkrt, sequencer, nextBaseTime)) {
        stopSequence(sequencer, vkrt, session);
        return;
    }
    sequencer->frameStartTimeUs = getMicroseconds();

    if (startSequenceRender(vkrt, sequencer) != 0) {
        LOG_ERROR("Failed to continue render sequence at frame %u", nextFrame);
        stopSequence(sequencer, vkrt, session);
    }
}
