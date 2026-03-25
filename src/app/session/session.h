#pragma once

#include <stdint.h>

#include "constants.h"
#include "vkrt_types.h"

typedef enum SessionRenderCommand {
    SESSION_RENDER_COMMAND_NONE = 0,
    SESSION_RENDER_COMMAND_START,
    SESSION_RENDER_COMMAND_STOP,
    SESSION_RENDER_COMMAND_RESET_ACCUMULATION,
} SessionRenderCommand;

typedef struct EditorUIState EditorUIState;
typedef struct DialogState DialogState;

typedef struct SessionRenderSettings {
    uint32_t width;
    uint32_t height;
    uint32_t targetSamples;
} SessionRenderSettings;

typedef struct SessionRenderTimer {
    uint8_t wasActive;
    uint64_t startTimeUs;
    float completedSeconds;
} SessionRenderTimer;

typedef struct SessionSceneObject {
    mat4 localTransform;
    uint32_t parentIndex;
    uint32_t meshIndex;
    vec3 localPosition;
    vec3 localRotation;
    vec3 localScale;
    char name[VKRT_NAME_LEN];
} SessionSceneObject;

typedef struct SessionMeshRecord {
    uint32_t importBatchIndex;
    uint32_t importLocalIndex;
} SessionMeshRecord;

typedef struct SessionTextureRecord {
    char* sourcePath;
    uint32_t colorSpace;
} SessionTextureRecord;

typedef struct SessionEditorState {
    uint32_t propertiesPanelIndex;
    uint32_t selectedSceneObjectIndex;
    char* currentScenePath;
    char* environmentTexturePath;
    EditorUIState* uiState;
    DialogState* dialogState;
    uint8_t requestMeshImportDialog;
    uint8_t requestTextureImportDialog;
    uint8_t requestEnvironmentImportDialog;
    uint8_t requestSceneOpenDialog;
    uint8_t requestSceneSaveDialog;
    uint8_t requestRenderSaveDialog;
    uint32_t requestedTextureMaterialIndex;
    uint32_t requestedTextureSlot;
    SessionRenderSettings renderConfig;
    SessionSceneObject* sceneObjects;
    uint32_t sceneObjectCount;
    uint32_t sceneObjectCapacity;
    char** meshImportPaths;
    uint32_t meshImportBatchCount;
    uint32_t meshImportBatchCapacity;
    SessionMeshRecord* meshRecords;
    uint32_t meshRecordCount;
    uint32_t meshRecordCapacity;
    SessionTextureRecord* textureRecords;
    uint32_t textureRecordCount;
    uint32_t textureRecordCapacity;
} SessionEditorState;

typedef struct SessionCommandQueue {
    uint32_t sceneObjectToRemove;
    uint32_t meshToRemove;
    char* sceneOpenPath;
    char* sceneSavePath;
    char* meshImportPath;
    char* textureImportPath;
    uint32_t textureImportMaterialIndex;
    uint32_t textureImportSlot;
    char* environmentImportPath;
    uint8_t clearEnvironmentRequested;
    char* saveImagePath;
    SessionRenderCommand renderCommand;
    SessionRenderSettings pendingRenderJob;
} SessionCommandQueue;

typedef struct SessionRuntimeState {
    SessionRenderTimer renderTimer;
    uint32_t lastSyncedSelectedMeshIndex;
} SessionRuntimeState;

typedef struct Session {
    SessionEditorState editor;
    SessionCommandQueue commands;
    SessionRuntimeState runtime;
} Session;

void sessionInit(Session* session);
void sessionDeinit(Session* session);

void sessionRequestMeshImportDialog(Session* session);
void sessionRequestTextureImportDialog(Session* session, uint32_t materialIndex, uint32_t textureSlot);
void sessionRequestEnvironmentImportDialog(Session* session);
void sessionRequestSceneOpenDialog(Session* session);
void sessionRequestSceneSaveDialog(Session* session);
void sessionRequestRenderSaveDialog(Session* session);
int sessionTakeMeshImportDialogRequest(Session* session);
int sessionTakeTextureImportDialogRequest(Session* session, uint32_t* outMaterialIndex, uint32_t* outTextureSlot);
int sessionTakeEnvironmentImportDialogRequest(Session* session);
int sessionTakeSceneOpenDialogRequest(Session* session);
int sessionTakeSceneSaveDialogRequest(Session* session);
int sessionTakeRenderSaveDialogRequest(Session* session);

void sessionQueueSceneOpen(Session* session, const char* path);
void sessionQueueSceneSave(Session* session, const char* path);
void sessionQueueMeshImport(Session* session, const char* path);
void sessionQueueTextureImport(Session* session, uint32_t materialIndex, uint32_t textureSlot, const char* path);
void sessionQueueEnvironmentImport(Session* session, const char* path);
void sessionQueueEnvironmentClear(Session* session);
void sessionQueueSceneObjectRemoval(Session* session, uint32_t objectIndex);
void sessionQueueMeshRemoval(Session* session, uint32_t meshIndex);
int sessionTakeSceneOpen(Session* session, char** outPath);
int sessionTakeSceneSave(Session* session, char** outPath);
int sessionTakeSceneObjectRemoval(Session* session, uint32_t* outObjectIndex);
int sessionTakeMeshImport(Session* session, char** outPath);
int sessionTakeTextureImport(Session* session, uint32_t* outMaterialIndex, uint32_t* outTextureSlot, char** outPath);
int sessionTakeEnvironmentImport(Session* session, char** outPath);
int sessionTakeEnvironmentClear(Session* session);
int sessionTakeMeshRemoval(Session* session, uint32_t* outMeshIndex);

void sessionQueueRenderSave(Session* session, const char* path);
void sessionQueueRenderStart(Session* session, uint32_t width, uint32_t height, uint32_t targetSamples);
void sessionQueueRenderStop(Session* session);
void sessionQueueRenderResetAccumulation(Session* session);
int sessionTakeRenderCommand(Session* session, SessionRenderCommand* outCommand, SessionRenderSettings* outSettings);
int sessionTakeRenderSave(Session* session, char** outPath);
void sessionSetCurrentScenePath(Session* session, const char* path);
const char* sessionGetCurrentScenePath(const Session* session);
void sessionSetEnvironmentTexturePath(Session* session, const char* path);
void sessionClearEnvironmentTexturePath(Session* session);
const char* sessionGetEnvironmentTexturePath(const Session* session);

uint32_t sessionGetSceneObjectCount(const Session* session);
const SessionSceneObject* sessionGetSceneObject(const Session* session, uint32_t objectIndex);
uint32_t sessionGetSelectedSceneObject(const Session* session);
void sessionSetSelectedSceneObject(Session* session, uint32_t objectIndex);
int sessionAddSceneObject(
    Session* session,
    const char* name,
    uint32_t parentIndex,
    uint32_t meshIndex,
    vec3 localPosition,
    vec3 localRotation,
    vec3 localScale,
    uint32_t* outObjectIndex
);
void sessionTruncateSceneObjects(Session* session, uint32_t objectCount);
uint32_t sessionFindSceneObjectForMesh(const Session* session, uint32_t meshIndex);
void sessionSelectSceneObjectForMesh(Session* session, uint32_t meshIndex);
int sessionSetSceneObjectName(Session* session, uint32_t objectIndex, const char* name);
int sessionSetSceneObjectMesh(Session* session, uint32_t objectIndex, uint32_t meshIndex);
int sessionSetSceneObjectLocalTransform(Session* session, uint32_t objectIndex, vec3 position, vec3 rotation, vec3 scale);
int sessionSetSceneObjectLocalTransformMatrix(Session* session, uint32_t objectIndex, mat4 localTransform);
int sessionSetSceneObjectLocalTransformForMesh(Session* session, uint32_t meshIndex, vec3 position, vec3 rotation, vec3 scale);
void sessionRemoveSceneObjectSubtree(Session* session, uint32_t objectIndex);
void sessionRemoveMeshReferencesNoPrune(Session* session, uint32_t meshIndex);
void sessionRemoveMeshReferences(Session* session, uint32_t meshIndex);
int sessionSyncSceneObjectTransforms(VKRT* vkrt, Session* session);
uint32_t sessionCountSceneObjectChildren(const Session* session, uint32_t objectIndex);
void sessionResetSceneState(Session* session);
int sessionRegisterMeshImportBatch(Session* session, const char* sourcePath, uint32_t meshCount);
int sessionAppendImportedTextureRecords(Session* session, uint32_t textureCount);
int sessionAppendStandaloneTextureRecord(Session* session, const char* sourcePath, uint32_t colorSpace);
void sessionTruncateTextureRecords(Session* session, uint32_t textureCount);
void sessionRemoveMeshRecord(Session* session, uint32_t meshIndex);
const char* sessionGetMeshImportPath(const Session* session, uint32_t batchIndex);
uint32_t sessionGetMeshRecordCount(const Session* session);
const SessionMeshRecord* sessionGetMeshRecord(const Session* session, uint32_t meshIndex);
uint32_t sessionGetTextureRecordCount(const Session* session);
const SessionTextureRecord* sessionGetTextureRecord(const Session* session, uint32_t textureIndex);
