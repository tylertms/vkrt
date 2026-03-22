#include "session.h"
#include "debug.h"
#include "io.h"
#include "numeric.h"
#include "vkrt.h"

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

static void buildLocalTransformMatrix(const vec3 position, const vec3 rotationDegrees, const vec3 scale, mat4 outMatrix) {
    if (!position || !rotationDegrees || !scale || !outMatrix) return;

    glm_mat4_identity(outMatrix);
    glm_translate(outMatrix, (vec3){position[0], position[1], position[2]});
    glm_rotate(outMatrix, glm_rad(rotationDegrees[2]), (vec3){0.0f, 0.0f, 1.0f});
    glm_rotate(outMatrix, glm_rad(rotationDegrees[1]), (vec3){0.0f, 1.0f, 0.0f});
    glm_rotate(outMatrix, glm_rad(rotationDegrees[0]), (vec3){1.0f, 0.0f, 0.0f});
    glm_scale(outMatrix, (vec3){scale[0], scale[1], scale[2]});
}

static int sceneObjectTransformMatrixValid(mat4 transform) {
    if (!transform) return 0;

    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            if (!isfinite(transform[column][row])) return 0;
        }
    }

    mat3 linear = GLM_MAT3_IDENTITY_INIT;
    for (int column = 0; column < 3; column++) {
        for (int row = 0; row < 3; row++) {
            linear[column][row] = transform[column][row];
        }
    }

    float determinant = glm_mat3_det(linear);
    return isfinite(determinant) && fabsf(determinant) >= 1e-8f;
}

static int ensureSceneObjectCapacity(Session* session, uint32_t additionalCount) {
    if (!session) return 0;
    if (additionalCount == 0u) return 1;
    if (session->editor.sceneObjectCount > UINT32_MAX - additionalCount) return 0;

    uint32_t requiredCount = session->editor.sceneObjectCount + additionalCount;
    if (requiredCount <= session->editor.sceneObjectCapacity) return 1;

    uint32_t nextCapacity = session->editor.sceneObjectCapacity > 0u ? session->editor.sceneObjectCapacity : 16u;
    while (nextCapacity < requiredCount) {
        if (nextCapacity > UINT32_MAX / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }

    SessionSceneObject* resized = (SessionSceneObject*)realloc(
        session->editor.sceneObjects,
        (size_t)nextCapacity * sizeof(*resized)
    );
    if (!resized) return 0;

    session->editor.sceneObjects = resized;
    session->editor.sceneObjectCapacity = nextCapacity;
    return 1;
}

static void removeSceneObjectAt(Session* session, uint32_t objectIndex) {
    if (!session || objectIndex >= session->editor.sceneObjectCount) return;

    uint32_t lastIndex = session->editor.sceneObjectCount - 1u;
    if (objectIndex != lastIndex) {
        memmove(
            &session->editor.sceneObjects[objectIndex],
            &session->editor.sceneObjects[objectIndex + 1u],
            (size_t)(lastIndex - objectIndex) * sizeof(SessionSceneObject)
        );
    }
    session->editor.sceneObjectCount = lastIndex;

    for (uint32_t i = 0; i < session->editor.sceneObjectCount; i++) {
        SessionSceneObject* object = &session->editor.sceneObjects[i];
        if (object->parentIndex == objectIndex) {
            object->parentIndex = VKRT_INVALID_INDEX;
        } else if (object->parentIndex != VKRT_INVALID_INDEX && object->parentIndex > objectIndex) {
            object->parentIndex--;
        }
    }

    if (session->editor.selectedSceneObjectIndex == objectIndex) {
        session->editor.selectedSceneObjectIndex = VKRT_INVALID_INDEX;
    } else if (session->editor.selectedSceneObjectIndex != VKRT_INVALID_INDEX &&
               session->editor.selectedSceneObjectIndex > objectIndex) {
        session->editor.selectedSceneObjectIndex--;
    }
}

static int sceneObjectHasChildren(const Session* session, uint32_t objectIndex) {
    if (!session) return 0;
    for (uint32_t i = 0; i < session->editor.sceneObjectCount; i++) {
        if (session->editor.sceneObjects[i].parentIndex == objectIndex) return 1;
    }
    return 0;
}

static int sceneObjectDescendsFrom(const Session* session, uint32_t objectIndex, uint32_t ancestorIndex) {
    if (!session || objectIndex >= session->editor.sceneObjectCount) return 0;

    uint32_t currentIndex = objectIndex;
    uint32_t maxDepth = session->editor.sceneObjectCount;
    while (currentIndex != VKRT_INVALID_INDEX && currentIndex < session->editor.sceneObjectCount && maxDepth-- > 0u) {
        if (currentIndex == ancestorIndex) return 1;
        currentIndex = session->editor.sceneObjects[currentIndex].parentIndex;
    }
    return 0;
}

static void pruneEmptyLeafGroups(Session* session) {
    if (!session) return;
    for (uint32_t i = session->editor.sceneObjectCount; i > 0u; i--) {
        uint32_t objectIndex = i - 1u;
        const SessionSceneObject* object = &session->editor.sceneObjects[objectIndex];
        if (object->meshIndex != VKRT_INVALID_INDEX) continue;
        if (sceneObjectHasChildren(session, objectIndex)) continue;
        removeSceneObjectAt(session, objectIndex);
    }
}

static int syncSceneObjectSubtree(VKRT* vkrt, Session* session, uint32_t objectIndex, mat4 parentWorldTransform) {
    if (!vkrt || !session || objectIndex >= session->editor.sceneObjectCount) return 0;

    const SessionSceneObject* object = &session->editor.sceneObjects[objectIndex];
    mat4 localTransform = GLM_MAT4_IDENTITY_INIT;
    mat4 worldTransform = GLM_MAT4_IDENTITY_INIT;
    memcpy(localTransform, object->localTransform, sizeof(localTransform));
    glm_mat4_mul(parentWorldTransform, localTransform, worldTransform);

    if (object->meshIndex != VKRT_INVALID_INDEX &&
        VKRT_setMeshTransformMatrix(vkrt, object->meshIndex, worldTransform) != VKRT_SUCCESS) {
        return 0;
    }

    for (uint32_t childIndex = 0; childIndex < session->editor.sceneObjectCount; childIndex++) {
        if (session->editor.sceneObjects[childIndex].parentIndex != objectIndex) continue;
        if (!syncSceneObjectSubtree(vkrt, session, childIndex, worldTransform)) {
            return 0;
        }
    }

    return 1;
}

void sessionInit(Session* session) {
    if (!session) return;

    memset(session, 0, sizeof(*session));
    session->commands.sceneObjectToRemove = VKRT_INVALID_INDEX;
    session->commands.meshToRemove = VKRT_INVALID_INDEX;
    session->commands.textureImportMaterialIndex = VKRT_INVALID_INDEX;
    session->commands.textureImportSlot = VKRT_INVALID_INDEX;
    session->editor.requestedTextureMaterialIndex = VKRT_INVALID_INDEX;
    session->editor.requestedTextureSlot = VKRT_INVALID_INDEX;
    session->editor.selectedSceneObjectIndex = VKRT_INVALID_INDEX;
    session->runtime.lastSyncedSelectedMeshIndex = VKRT_INVALID_INDEX;
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
    clearOwnedString(&session->commands.textureImportPath);
    clearOwnedString(&session->commands.saveImagePath);
    clearOwnedString(&session->editor.renderSequenceFolderPath);
    free(session->editor.sceneObjects);
    session->editor.sceneObjects = NULL;
    session->editor.sceneObjectCount = 0;
    session->editor.sceneObjectCapacity = 0;
}

void sessionRequestMeshImportDialog(Session* session) {
    if (!session) return;
    session->editor.requestMeshImportDialog = 1;
}

void sessionRequestEnvironmentImportDialog(Session* session) {
    if (!session) return;
    session->editor.requestEnvironmentImportDialog = 1;
}

void sessionRequestTextureImportDialog(Session* session, uint32_t materialIndex, uint32_t textureSlot) {
    if (!session) return;
    session->editor.requestTextureImportDialog = 1;
    session->editor.requestedTextureMaterialIndex = materialIndex;
    session->editor.requestedTextureSlot = textureSlot;
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

int sessionTakeTextureImportDialogRequest(Session* session, uint32_t* outMaterialIndex, uint32_t* outTextureSlot) {
    if (!session || !session->editor.requestTextureImportDialog) return 0;

    session->editor.requestTextureImportDialog = 0;
    if (outMaterialIndex) *outMaterialIndex = session->editor.requestedTextureMaterialIndex;
    if (outTextureSlot) *outTextureSlot = session->editor.requestedTextureSlot;
    session->editor.requestedTextureMaterialIndex = VKRT_INVALID_INDEX;
    session->editor.requestedTextureSlot = VKRT_INVALID_INDEX;
    return 1;
}

int sessionTakeEnvironmentImportDialogRequest(Session* session) {
    if (!session || !session->editor.requestEnvironmentImportDialog) return 0;
    session->editor.requestEnvironmentImportDialog = 0;
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

void sessionQueueTextureImport(Session* session, uint32_t materialIndex, uint32_t textureSlot, const char* path) {
    if (!session) return;

    clearOwnedString(&session->commands.textureImportPath);
    session->commands.textureImportMaterialIndex = VKRT_INVALID_INDEX;
    session->commands.textureImportSlot = VKRT_INVALID_INDEX;
    if (!path || !path[0]) return;

    session->commands.textureImportPath = stringDuplicate(path);
    if (!session->commands.textureImportPath) return;
    session->commands.textureImportMaterialIndex = materialIndex;
    session->commands.textureImportSlot = textureSlot;
}

void sessionQueueEnvironmentImport(Session* session, const char* path) {
    if (!session) return;

    clearOwnedString(&session->commands.environmentImportPath);
    if (!path || !path[0]) return;
    session->commands.environmentImportPath = stringDuplicate(path);
}

void sessionQueueSceneObjectRemoval(Session* session, uint32_t objectIndex) {
    if (!session) return;
    session->commands.sceneObjectToRemove =
        objectIndex < session->editor.sceneObjectCount ? objectIndex : VKRT_INVALID_INDEX;
}

void sessionQueueMeshRemoval(Session* session, uint32_t meshIndex) {
    if (!session) return;
    session->commands.meshToRemove = meshIndex;
}

int sessionTakeMeshImport(Session* session, char** outPath) {
    if (!session || !session->commands.meshImportPath) return 0;

    char* path = session->commands.meshImportPath;
    session->commands.meshImportPath = NULL;

    if (outPath) *outPath = path;
    else free(path);

    return 1;
}

int sessionTakeTextureImport(Session* session, uint32_t* outMaterialIndex, uint32_t* outTextureSlot, char** outPath) {
    if (!session || !session->commands.textureImportPath) return 0;

    char* path = session->commands.textureImportPath;
    session->commands.textureImportPath = NULL;

    if (outMaterialIndex) *outMaterialIndex = session->commands.textureImportMaterialIndex;
    if (outTextureSlot) *outTextureSlot = session->commands.textureImportSlot;

    session->commands.textureImportMaterialIndex = VKRT_INVALID_INDEX;
    session->commands.textureImportSlot = VKRT_INVALID_INDEX;

    if (outPath) *outPath = path;
    else free(path);

    return 1;
}

int sessionTakeEnvironmentImport(Session* session, char** outPath) {
    if (!session || !session->commands.environmentImportPath) return 0;

    char* path = session->commands.environmentImportPath;
    session->commands.environmentImportPath = NULL;

    if (outPath) *outPath = path;
    else free(path);

    return 1;
}

int sessionTakeSceneObjectRemoval(Session* session, uint32_t* outObjectIndex) {
    if (!session || session->commands.sceneObjectToRemove == VKRT_INVALID_INDEX) return 0;

    if (outObjectIndex) *outObjectIndex = session->commands.sceneObjectToRemove;
    session->commands.sceneObjectToRemove = VKRT_INVALID_INDEX;
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

uint32_t sessionGetSceneObjectCount(const Session* session) {
    return session ? session->editor.sceneObjectCount : 0u;
}

const SessionSceneObject* sessionGetSceneObject(const Session* session, uint32_t objectIndex) {
    if (!session || objectIndex >= session->editor.sceneObjectCount) return NULL;
    return &session->editor.sceneObjects[objectIndex];
}

uint32_t sessionGetSelectedSceneObject(const Session* session) {
    if (!session) return VKRT_INVALID_INDEX;
    return session->editor.selectedSceneObjectIndex;
}

void sessionSetSelectedSceneObject(Session* session, uint32_t objectIndex) {
    if (!session) return;
    session->editor.selectedSceneObjectIndex =
        objectIndex < session->editor.sceneObjectCount ? objectIndex : VKRT_INVALID_INDEX;
}

int sessionAddSceneObject(
    Session* session,
    const char* name,
    uint32_t parentIndex,
    uint32_t meshIndex,
    vec3 localPosition,
    vec3 localRotation,
    vec3 localScale,
    uint32_t* outObjectIndex
) {
    if (!session) return 0;
    if (parentIndex != VKRT_INVALID_INDEX && parentIndex >= session->editor.sceneObjectCount) return 0;
    if (!ensureSceneObjectCapacity(session, 1u)) return 0;

    uint32_t objectIndex = session->editor.sceneObjectCount++;
    SessionSceneObject* object = &session->editor.sceneObjects[objectIndex];
    memset(object, 0, sizeof(*object));
    object->parentIndex = parentIndex;
    object->meshIndex = meshIndex;
    object->localScale[0] = 1.0f;
    object->localScale[1] = 1.0f;
    object->localScale[2] = 1.0f;
    if (localPosition) glm_vec3_copy(localPosition, object->localPosition);
    if (localRotation) glm_vec3_copy(localRotation, object->localRotation);
    if (localScale) glm_vec3_copy(localScale, object->localScale);
    buildLocalTransformMatrix(object->localPosition, object->localRotation, object->localScale, object->localTransform);
    snprintf(object->name, sizeof(object->name), "%s", (name && name[0]) ? name : "Object");
    if (outObjectIndex) *outObjectIndex = objectIndex;
    return 1;
}

void sessionTruncateSceneObjects(Session* session, uint32_t objectCount) {
    if (!session) return;
    if (objectCount >= session->editor.sceneObjectCount) return;
    session->editor.sceneObjectCount = objectCount;
    if (session->editor.selectedSceneObjectIndex >= session->editor.sceneObjectCount) {
        session->editor.selectedSceneObjectIndex = VKRT_INVALID_INDEX;
    }
}

uint32_t sessionFindSceneObjectForMesh(const Session* session, uint32_t meshIndex) {
    if (!session) return VKRT_INVALID_INDEX;
    for (uint32_t i = 0; i < session->editor.sceneObjectCount; i++) {
        if (session->editor.sceneObjects[i].meshIndex == meshIndex) return i;
    }
    return VKRT_INVALID_INDEX;
}

void sessionSelectSceneObjectForMesh(Session* session, uint32_t meshIndex) {
    if (!session) return;
    session->editor.selectedSceneObjectIndex = sessionFindSceneObjectForMesh(session, meshIndex);
}

int sessionSetSceneObjectName(Session* session, uint32_t objectIndex, const char* name) {
    if (!session || objectIndex >= session->editor.sceneObjectCount || !name) return 0;
    snprintf(session->editor.sceneObjects[objectIndex].name, VKRT_NAME_LEN, "%s", name);
    return 1;
}

int sessionSetSceneObjectMesh(Session* session, uint32_t objectIndex, uint32_t meshIndex) {
    if (!session || objectIndex >= session->editor.sceneObjectCount) return 0;
    session->editor.sceneObjects[objectIndex].meshIndex = meshIndex;
    return 1;
}

int sessionSetSceneObjectLocalTransform(Session* session, uint32_t objectIndex, vec3 position, vec3 rotation, vec3 scale) {
    if (!session || objectIndex >= session->editor.sceneObjectCount) return 0;
    if (!position || !rotation || !scale) return 0;
    SessionSceneObject* object = &session->editor.sceneObjects[objectIndex];
    glm_vec3_copy(position, object->localPosition);
    glm_vec3_copy(rotation, object->localRotation);
    glm_vec3_copy(scale, object->localScale);
    buildLocalTransformMatrix(object->localPosition, object->localRotation, object->localScale, object->localTransform);
    return 1;
}

int sessionSetSceneObjectLocalTransformMatrix(Session* session, uint32_t objectIndex, mat4 localTransform) {
    if (!session || objectIndex >= session->editor.sceneObjectCount || !sceneObjectTransformMatrixValid(localTransform)) {
        return 0;
    }

    SessionSceneObject* object = &session->editor.sceneObjects[objectIndex];
    memcpy(object->localTransform, localTransform, sizeof(object->localTransform));
    decomposeImportedMeshTransform(object->localTransform, object->localPosition, object->localRotation, object->localScale);
    return 1;
}

int sessionSetSceneObjectLocalTransformForMesh(Session* session, uint32_t meshIndex, vec3 position, vec3 rotation, vec3 scale) {
    uint32_t objectIndex = sessionFindSceneObjectForMesh(session, meshIndex);
    if (objectIndex == VKRT_INVALID_INDEX) return 0;
    return sessionSetSceneObjectLocalTransform(session, objectIndex, position, rotation, scale);
}

void sessionRemoveSceneObjectSubtree(Session* session, uint32_t objectIndex) {
    if (!session || objectIndex >= session->editor.sceneObjectCount) return;

    for (uint32_t i = session->editor.sceneObjectCount; i > 0u; i--) {
        uint32_t candidateIndex = i - 1u;
        if (!sceneObjectDescendsFrom(session, candidateIndex, objectIndex)) continue;
        removeSceneObjectAt(session, candidateIndex);
    }
}

void sessionRemoveMeshReferencesNoPrune(Session* session, uint32_t meshIndex) {
    if (!session) return;

    for (uint32_t i = 0; i < session->editor.sceneObjectCount; i++) {
        SessionSceneObject* object = &session->editor.sceneObjects[i];
        if (object->meshIndex == meshIndex) {
            object->meshIndex = VKRT_INVALID_INDEX;
        } else if (object->meshIndex != VKRT_INVALID_INDEX && object->meshIndex > meshIndex) {
            object->meshIndex--;
        }
    }
}

void sessionRemoveMeshReferences(Session* session, uint32_t meshIndex) {
    sessionRemoveMeshReferencesNoPrune(session, meshIndex);
    pruneEmptyLeafGroups(session);
}

int sessionSyncSceneObjectTransforms(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return 0;
    mat4 identity = GLM_MAT4_IDENTITY_INIT;
    for (uint32_t i = 0; i < session->editor.sceneObjectCount; i++) {
        if (session->editor.sceneObjects[i].parentIndex != VKRT_INVALID_INDEX) continue;
        if (!syncSceneObjectSubtree(vkrt, session, i, identity)) {
            return 0;
        }
    }
    return 1;
}

uint32_t sessionCountSceneObjectChildren(const Session* session, uint32_t objectIndex) {
    if (!session) return 0;
    uint32_t count = 0u;
    for (uint32_t i = 0; i < session->editor.sceneObjectCount; i++) {
        if (session->editor.sceneObjects[i].parentIndex == objectIndex) count++;
    }
    return count;
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

    qsort(
        timeline->keyframes,
        timeline->keyframeCount,
        sizeof(timeline->keyframes[0]),
        vkrtCompareSceneTimelineKeyframesByTime
    );
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
