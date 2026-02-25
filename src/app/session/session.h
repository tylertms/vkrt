#pragma once

#include <stdint.h>

typedef enum SessionRenderCommand {
    SESSION_RENDER_COMMAND_NONE = 0,
    SESSION_RENDER_COMMAND_START,
    SESSION_RENDER_COMMAND_STOP,
} SessionRenderCommand;

typedef struct SessionRenderAnimationSettings {
    uint8_t enabled;
    float minTime;
    float maxTime;
    float timeStep;
} SessionRenderAnimationSettings;

typedef struct SessionRenderSettings {
    uint32_t width;
    uint32_t height;
    uint32_t targetSamples;
    SessionRenderAnimationSettings animation;
} SessionRenderSettings;

typedef struct SessionSequenceProgress {
    uint8_t active;
    uint32_t frameIndex;
    uint32_t frameCount;
    float currentTime;
} SessionSequenceProgress;

typedef struct Session {
    char** meshNames;
    uint32_t meshCount;
    uint32_t meshToRemove;
    char* meshImportPath;
    char* saveImagePath;
    char* renderSequenceFolderPath;
    uint8_t requestMeshImportDialog;
    uint8_t requestRenderSaveDialog;
    uint8_t requestRenderSequenceFolderDialog;
    SessionSequenceProgress sequenceProgress;
    SessionRenderCommand renderCommand;
    SessionRenderSettings pendingRenderJob;
    SessionRenderSettings renderConfig;
} Session;

void sessionInit(Session* session);
void sessionDeinit(Session* session);

void sessionSetMeshName(Session* session, const char* filePath, uint32_t meshIndex);
void sessionRemoveMeshName(Session* session, uint32_t meshIndex);
const char* sessionGetMeshName(const Session* session, uint32_t meshIndex);

void sessionRequestMeshImportDialog(Session* session);
void sessionRequestRenderSaveDialog(Session* session);
void sessionRequestRenderSequenceFolderDialog(Session* session);
int sessionTakeMeshImportDialogRequest(Session* session);
int sessionTakeRenderSaveDialogRequest(Session* session);
int sessionTakeRenderSequenceFolderDialogRequest(Session* session);

void sessionQueueMeshImport(Session* session, const char* path);
void sessionQueueMeshRemoval(Session* session, uint32_t meshIndex);
int sessionTakeMeshImport(Session* session, char** outPath);
int sessionTakeMeshRemoval(Session* session, uint32_t* outMeshIndex);

void sessionQueueRenderSave(Session* session, const char* path);
void sessionQueueRenderStart(Session* session, uint32_t width, uint32_t height, uint32_t targetSamples, const SessionRenderAnimationSettings* animation);
void sessionQueueRenderStop(Session* session);
int sessionTakeRenderCommand(Session* session, SessionRenderCommand* outCommand, SessionRenderSettings* outSettings);
int sessionTakeRenderSave(Session* session, char** outPath);
void sessionSetRenderSequenceFolder(Session* session, const char* path);
const char* sessionGetRenderSequenceFolder(const Session* session);

uint32_t sessionComputeAnimationFrameCount(const SessionRenderAnimationSettings* animation);
void sessionSanitizeAnimationSettings(SessionRenderAnimationSettings* animation);
