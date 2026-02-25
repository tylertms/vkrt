#pragma once

#include <stdint.h>

typedef enum SessionRenderCommand {
    SESSION_RENDER_COMMAND_NONE = 0,
    SESSION_RENDER_COMMAND_START,
    SESSION_RENDER_COMMAND_STOP,
} SessionRenderCommand;

typedef struct SessionRenderSettings {
    uint32_t width;
    uint32_t height;
    uint32_t targetSamples;
} SessionRenderSettings;

typedef struct Session {
    char** meshNames;
    uint32_t meshCount;
    uint32_t meshToRemove;
    char* meshImportPath;
    char* saveImagePath;
    uint8_t requestMeshImportDialog;
    uint8_t requestRenderSaveDialog;
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
int sessionTakeMeshImportDialogRequest(Session* session);
int sessionTakeRenderSaveDialogRequest(Session* session);

void sessionQueueMeshImport(Session* session, const char* path);
void sessionQueueMeshRemoval(Session* session, uint32_t meshIndex);
int sessionTakeMeshImport(Session* session, char** outPath);
int sessionTakeMeshRemoval(Session* session, uint32_t* outMeshIndex);

void sessionQueueRenderSave(Session* session, const char* path);
void sessionQueueRenderStart(Session* session, uint32_t width, uint32_t height, uint32_t targetSamples);
void sessionQueueRenderStop(Session* session);
int sessionTakeRenderCommand(Session* session, SessionRenderCommand* outCommand, SessionRenderSettings* outSettings);
int sessionTakeRenderSave(Session* session, char** outPath);
