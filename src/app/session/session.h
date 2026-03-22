#pragma once

#include <stdint.h>

#include "constants.h"
#include "vkrt_types.h"

typedef enum SessionRenderCommand {
    SESSION_RENDER_COMMAND_NONE = 0,
    SESSION_RENDER_COMMAND_START,
    SESSION_RENDER_COMMAND_STOP,
} SessionRenderCommand;

enum { SESSION_SCENE_TIMELINE_KEYFRAME_CAPACITY = VKRT_SCENE_TIMELINE_MAX_KEYFRAMES };

#define SESSION_SCENE_TIMELINE_TIME_MIN 0.0f
#define SESSION_SCENE_TIMELINE_TIME_MAX 100000.0f
#define SESSION_SCENE_TIMELINE_EMISSION_SCALE_MIN 0.0f
#define SESSION_SCENE_TIMELINE_EMISSION_SCALE_MAX 1024.0f
#define SESSION_SCENE_TIMELINE_EMISSION_TINT_MIN 0.0f
#define SESSION_SCENE_TIMELINE_EMISSION_TINT_MAX 16.0f
#define SESSION_SCENE_TIMELINE_DEFAULT_INCREMENT 0.5f

typedef VKRT_SceneTimelineKeyframe SessionSceneTimelineKeyframe;
typedef VKRT_SceneTimelineSettings SessionSceneTimelineSettings;
typedef struct EditorUIState EditorUIState;
typedef struct DialogState DialogState;

typedef struct SessionRenderAnimationSettings {
    uint8_t enabled;
    float minTime;
    float maxTime;
    float timeStep;
    SessionSceneTimelineSettings sceneTimeline;
} SessionRenderAnimationSettings;

typedef struct SessionRenderSettings {
    uint32_t width;
    uint32_t height;
    uint32_t targetSamples;
    SessionRenderAnimationSettings animation;
} SessionRenderSettings;

enum {
    RENDER_SEQUENCE_PATH_CAPACITY = 1024,
    RENDER_SEQUENCE_ETA_WINDOW = 4
};

typedef struct RenderSequencer {
    uint8_t active;
    SessionRenderSettings renderSettings;
    uint32_t frameIndex;
    uint32_t frameCount;
    float minTime;
    float maxTime;
    float step;
    float currentTime;
    uint8_t hasEstimatedRemaining;
    float estimatedRemainingSeconds;
    uint64_t frameStartTimeUs;
    uint32_t timedFrameCount;
    float averageFrameSeconds;
    uint32_t recentFrameCount;
    uint32_t recentFrameWriteIndex;
    float recentFrameSumSeconds;
    float recentFrameSeconds[RENDER_SEQUENCE_ETA_WINDOW];
    char outputFolder[RENDER_SEQUENCE_PATH_CAPACITY];
} RenderSequencer;

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

typedef struct SessionEditorState {
    uint32_t propertiesPanelIndex;
    uint32_t selectedSceneObjectIndex;
    char* renderSequenceFolderPath;
    EditorUIState* uiState;
    DialogState* dialogState;
    uint8_t requestMeshImportDialog;
    uint8_t requestTextureImportDialog;
    uint8_t requestEnvironmentImportDialog;
    uint8_t requestRenderSaveDialog;
    uint8_t requestRenderSequenceFolderDialog;
    uint32_t requestedTextureMaterialIndex;
    uint32_t requestedTextureSlot;
    SessionRenderSettings renderConfig;
    SessionSceneObject* sceneObjects;
    uint32_t sceneObjectCount;
    uint32_t sceneObjectCapacity;
} SessionEditorState;

typedef struct SessionCommandQueue {
    uint32_t sceneObjectToRemove;
    uint32_t meshToRemove;
    char* meshImportPath;
    char* textureImportPath;
    uint32_t textureImportMaterialIndex;
    uint32_t textureImportSlot;
    char* environmentImportPath;
    char* saveImagePath;
    SessionRenderCommand renderCommand;
    SessionRenderSettings pendingRenderJob;
} SessionCommandQueue;

typedef struct SessionRuntimeState {
    RenderSequencer sequencer;
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
void sessionRequestRenderSaveDialog(Session* session);
void sessionRequestRenderSequenceFolderDialog(Session* session);
int sessionTakeMeshImportDialogRequest(Session* session);
int sessionTakeTextureImportDialogRequest(Session* session, uint32_t* outMaterialIndex, uint32_t* outTextureSlot);
int sessionTakeEnvironmentImportDialogRequest(Session* session);
int sessionTakeRenderSaveDialogRequest(Session* session);
int sessionTakeRenderSequenceFolderDialogRequest(Session* session);

void sessionQueueMeshImport(Session* session, const char* path);
void sessionQueueTextureImport(Session* session, uint32_t materialIndex, uint32_t textureSlot, const char* path);
void sessionQueueEnvironmentImport(Session* session, const char* path);
void sessionQueueSceneObjectRemoval(Session* session, uint32_t objectIndex);
void sessionQueueMeshRemoval(Session* session, uint32_t meshIndex);
int sessionTakeSceneObjectRemoval(Session* session, uint32_t* outObjectIndex);
int sessionTakeMeshImport(Session* session, char** outPath);
int sessionTakeTextureImport(Session* session, uint32_t* outMaterialIndex, uint32_t* outTextureSlot, char** outPath);
int sessionTakeEnvironmentImport(Session* session, char** outPath);
int sessionTakeMeshRemoval(Session* session, uint32_t* outMeshIndex);

void sessionQueueRenderSave(Session* session, const char* path);
void sessionQueueRenderStart(Session* session, uint32_t width, uint32_t height, uint32_t targetSamples, const SessionRenderAnimationSettings* animation);
void sessionQueueRenderStop(Session* session);
int sessionTakeRenderCommand(Session* session, SessionRenderCommand* outCommand, SessionRenderSettings* outSettings);
int sessionTakeRenderSave(Session* session, char** outPath);
void sessionSetRenderSequenceFolder(Session* session, const char* path);
const char* sessionGetRenderSequenceFolder(const Session* session);

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

uint32_t sessionComputeAnimationFrameCount(const SessionRenderAnimationSettings* animation);
void sessionSanitizeAnimationSettings(SessionRenderAnimationSettings* animation);
