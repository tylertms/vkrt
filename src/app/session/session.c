#include "session.h"

#include "../../../external/cglm/include/types.h"
#include "constants.h"
#include "io.h"
#include "vkrt.h"
#include "vkrt_types.h"

#include <affine-pre.h>
#include <affine.h>
#include <mat3.h>
#include <mat4.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>
#include <vec3.h>

static const uint32_t kDefaultRenderTargetSamples = 1024u;

static void clearOwnedString(char** value) {
    if (!value || !*value) return;
    free(*value);
    *value = NULL;
}

static void clearMeshImportPath(char** path) {
    if (!path || !*path) return;
    free(*path);
    *path = NULL;
}

static void clearTextureRecord(SessionTextureRecord* record) {
    if (!record) return;
    free(record->sourcePath);
    memset(record, 0, sizeof(*record));
}

static void buildLocalTransformMatrix(
    const vec3 position,
    const vec3 rotationDegrees,
    const vec3 scale,
    mat4 outMatrix
) {
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

    SessionSceneObject* resized =
        (SessionSceneObject*)realloc(session->editor.sceneObjects, (size_t)nextCapacity * sizeof(*resized));
    if (!resized) return 0;

    session->editor.sceneObjects = resized;
    session->editor.sceneObjectCapacity = nextCapacity;
    return 1;
}

static int ensureMeshImportBatchCapacity(Session* session, uint32_t additionalCount) {
    if (!session) return 0;
    if (additionalCount == 0u) return 1;
    if (session->editor.meshImportBatchCount > UINT32_MAX - additionalCount) return 0;

    uint32_t requiredCount = session->editor.meshImportBatchCount + additionalCount;
    if (requiredCount <= session->editor.meshImportBatchCapacity) return 1;

    uint32_t nextCapacity = session->editor.meshImportBatchCapacity > 0u ? session->editor.meshImportBatchCapacity : 8u;
    while (nextCapacity < requiredCount) {
        if (nextCapacity > UINT32_MAX / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }

    char** resized = (char**)realloc((void*)session->editor.meshImportPaths, (size_t)nextCapacity * sizeof(*resized));
    if (!resized) return 0;

    session->editor.meshImportPaths = resized;
    session->editor.meshImportBatchCapacity = nextCapacity;
    return 1;
}

static int ensureMeshRecordCapacity(Session* session, uint32_t additionalCount) {
    if (!session) return 0;
    if (additionalCount == 0u) return 1;
    if (session->editor.meshRecordCount > UINT32_MAX - additionalCount) return 0;

    uint32_t requiredCount = session->editor.meshRecordCount + additionalCount;
    if (requiredCount <= session->editor.meshRecordCapacity) return 1;

    uint32_t nextCapacity = session->editor.meshRecordCapacity > 0u ? session->editor.meshRecordCapacity : 16u;
    while (nextCapacity < requiredCount) {
        if (nextCapacity > UINT32_MAX / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }

    SessionMeshRecord* resized =
        (SessionMeshRecord*)realloc(session->editor.meshRecords, (size_t)nextCapacity * sizeof(*resized));
    if (!resized) return 0;

    session->editor.meshRecords = resized;
    session->editor.meshRecordCapacity = nextCapacity;
    return 1;
}

static int ensureTextureRecordCapacity(Session* session, uint32_t additionalCount) {
    if (!session) return 0;
    if (additionalCount == 0u) return 1;
    if (session->editor.textureRecordCount > UINT32_MAX - additionalCount) return 0;

    uint32_t requiredCount = session->editor.textureRecordCount + additionalCount;
    if (requiredCount <= session->editor.textureRecordCapacity) return 1;

    uint32_t nextCapacity = session->editor.textureRecordCapacity > 0u ? session->editor.textureRecordCapacity : 8u;
    while (nextCapacity < requiredCount) {
        if (nextCapacity > UINT32_MAX / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }

    SessionTextureRecord* resized =
        (SessionTextureRecord*)realloc(session->editor.textureRecords, (size_t)nextCapacity * sizeof(*resized));
    if (!resized) return 0;

    session->editor.textureRecords = resized;
    session->editor.textureRecordCapacity = nextCapacity;
    return 1;
}

static void clearSceneAssetState(Session* session) {
    if (!session) return;

    session->editor.sceneObjectCount = 0u;
    session->editor.selectedSceneObjectIndex = VKRT_INVALID_INDEX;
    session->runtime.lastSyncedSelectedMeshIndex = VKRT_INVALID_INDEX;

    for (uint32_t i = 0; i < session->editor.meshImportBatchCount; i++) {
        clearMeshImportPath(&session->editor.meshImportPaths[i]);
    }
    session->editor.meshImportBatchCount = 0u;

    session->editor.meshRecordCount = 0u;

    for (uint32_t i = 0; i < session->editor.textureRecordCount; i++) {
        clearTextureRecord(&session->editor.textureRecords[i]);
    }
    session->editor.textureRecordCount = 0u;
    clearOwnedString(&session->editor.environmentTexturePath);
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
    } else if (
        session->editor.selectedSceneObjectIndex != VKRT_INVALID_INDEX &&
        session->editor.selectedSceneObjectIndex > objectIndex
    ) {
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

typedef struct SceneObjectSyncNode {
    mat4 parentWorldTransform;
    uint32_t objectIndex;
} SceneObjectSyncNode;

static int pushSceneObjectSyncNode(
    SceneObjectSyncNode** stack,
    uint32_t* stackCount,
    uint32_t* stackCapacity,
    uint32_t objectIndex,
    mat4 parentWorldTransform
) {
    if (!stack || !stackCount || !stackCapacity) return 0;

    if (*stackCount == *stackCapacity) {
        uint32_t nextCapacity = *stackCapacity > 0u ? (*stackCapacity * 2u) : 16u;
        SceneObjectSyncNode* resized = (SceneObjectSyncNode*)realloc(*stack, (size_t)nextCapacity * sizeof(*resized));
        if (!resized) return 0;
        *stack = resized;
        *stackCapacity = nextCapacity;
    }

    SceneObjectSyncNode* node = &(*stack)[*stackCount];
    memcpy(node->parentWorldTransform, parentWorldTransform, sizeof(node->parentWorldTransform));
    node->objectIndex = objectIndex;
    (*stackCount)++;
    return 1;
}

static int syncSceneObjectTransformsIterative(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return 0;

    SceneObjectSyncNode* stack = NULL;
    uint32_t stackCount = 0u;
    uint32_t stackCapacity = 0u;
    mat4 identity = GLM_MAT4_IDENTITY_INIT;

    for (uint32_t objectIndex = session->editor.sceneObjectCount; objectIndex > 0u; objectIndex--) {
        uint32_t rootIndex = objectIndex - 1u;
        if (session->editor.sceneObjects[rootIndex].parentIndex != VKRT_INVALID_INDEX) continue;
        if (!pushSceneObjectSyncNode(&stack, &stackCount, &stackCapacity, rootIndex, identity)) {
            free(stack);
            return 0;
        }
    }

    while (stackCount > 0u) {
        SceneObjectSyncNode node = stack[--stackCount];
        const SessionSceneObject* object = &session->editor.sceneObjects[node.objectIndex];
        mat4 localTransform = GLM_MAT4_IDENTITY_INIT;
        mat4 worldTransform = GLM_MAT4_IDENTITY_INIT;
        memcpy(localTransform, object->localTransform, sizeof(localTransform));
        glm_mat4_mul(node.parentWorldTransform, localTransform, worldTransform);

        if (object->meshIndex != VKRT_INVALID_INDEX &&
            VKRT_setMeshTransformMatrix(vkrt, object->meshIndex, worldTransform) != VKRT_SUCCESS) {
            free(stack);
            return 0;
        }

        for (uint32_t childIndex = session->editor.sceneObjectCount; childIndex > 0u; childIndex--) {
            uint32_t nextChildIndex = childIndex - 1u;
            if (session->editor.sceneObjects[nextChildIndex].parentIndex != node.objectIndex) continue;
            if (!pushSceneObjectSyncNode(&stack, &stackCount, &stackCapacity, nextChildIndex, worldTransform)) {
                free(stack);
                return 0;
            }
        }
    }

    free(stack);
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
    VKRT_defaultRenderExportSettings(&session->editor.renderExportSettings);
    session->commands.saveImageSettings = session->editor.renderExportSettings;
}

void sessionDeinit(Session* session) {
    if (!session) return;

    clearOwnedString(&session->commands.sceneOpenPath);
    clearOwnedString(&session->commands.sceneSavePath);
    clearOwnedString(&session->commands.meshImportPath);
    clearOwnedString(&session->commands.textureImportPath);
    clearOwnedString(&session->commands.environmentImportPath);
    clearOwnedString(&session->commands.saveImagePath);
    clearOwnedString(&session->editor.currentScenePath);
    clearOwnedString(&session->editor.environmentTexturePath);

    for (uint32_t i = 0; i < session->editor.meshImportBatchCount; i++) {
        clearMeshImportPath(&session->editor.meshImportPaths[i]);
    }
    for (uint32_t i = 0; i < session->editor.textureRecordCount; i++) {
        clearTextureRecord(&session->editor.textureRecords[i]);
    }

    free((void*)session->editor.sceneObjects);
    free((void*)session->editor.meshImportPaths);
    free(session->editor.meshRecords);
    free(session->editor.textureRecords);
    session->editor.sceneObjects = NULL;
    session->editor.sceneObjectCount = 0;
    session->editor.sceneObjectCapacity = 0;
    session->editor.meshImportPaths = NULL;
    session->editor.meshImportBatchCount = 0;
    session->editor.meshImportBatchCapacity = 0;
    session->editor.meshRecords = NULL;
    session->editor.meshRecordCount = 0;
    session->editor.meshRecordCapacity = 0;
    session->editor.textureRecords = NULL;
    session->editor.textureRecordCount = 0;
    session->editor.textureRecordCapacity = 0;
}

void sessionRequestMeshImportDialog(Session* session) {
    if (!session) return;
    session->editor.requestMeshImportDialog = 1;
}

void sessionRequestEnvironmentImportDialog(Session* session) {
    if (!session) return;
    session->editor.requestEnvironmentImportDialog = 1;
}

void sessionRequestSceneOpenDialog(Session* session) {
    if (!session) return;
    session->editor.requestSceneOpenDialog = 1;
}

void sessionRequestSceneSaveDialog(Session* session) {
    if (!session) return;
    session->editor.requestSceneSaveDialog = 1;
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

int sessionTakeSceneOpenDialogRequest(Session* session) {
    if (!session || !session->editor.requestSceneOpenDialog) return 0;
    session->editor.requestSceneOpenDialog = 0;
    return 1;
}

int sessionTakeSceneSaveDialogRequest(Session* session) {
    if (!session || !session->editor.requestSceneSaveDialog) return 0;
    session->editor.requestSceneSaveDialog = 0;
    return 1;
}

int sessionTakeRenderSaveDialogRequest(Session* session) {
    if (!session || !session->editor.requestRenderSaveDialog) return 0;
    session->editor.requestRenderSaveDialog = 0;
    return 1;
}

void sessionQueueMeshImport(Session* session, const char* path) {
    if (!session) return;

    clearOwnedString(&session->commands.meshImportPath);
    if (!path || !path[0]) return;
    session->commands.meshImportPath = stringDuplicate(path);
}

void sessionQueueSceneOpen(Session* session, const char* path) {
    if (!session) return;

    clearOwnedString(&session->commands.sceneOpenPath);
    if (!path || !path[0]) return;
    session->commands.sceneOpenPath = stringDuplicate(path);
}

void sessionQueueSceneSave(Session* session, const char* path) {
    if (!session) return;

    clearOwnedString(&session->commands.sceneSavePath);
    if (!path || !path[0]) return;
    session->commands.sceneSavePath = stringDuplicate(path);
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

void sessionQueueEnvironmentClear(Session* session) {
    if (!session) return;
    session->commands.clearEnvironmentRequested = 1u;
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

    if (outPath) {
        *outPath = path;
    } else {
        free(path);
    }

    return 1;
}

int sessionTakeSceneOpen(Session* session, char** outPath) {
    if (!session || !session->commands.sceneOpenPath) return 0;

    char* path = session->commands.sceneOpenPath;
    session->commands.sceneOpenPath = NULL;

    if (outPath) {
        *outPath = path;
    } else {
        free(path);
    }

    return 1;
}

int sessionTakeSceneSave(Session* session, char** outPath) {
    if (!session || !session->commands.sceneSavePath) return 0;

    char* path = session->commands.sceneSavePath;
    session->commands.sceneSavePath = NULL;

    if (outPath) {
        *outPath = path;
    } else {
        free(path);
    }

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

    if (outPath) {
        *outPath = path;
    } else {
        free(path);
    }

    return 1;
}

int sessionTakeEnvironmentImport(Session* session, char** outPath) {
    if (!session || !session->commands.environmentImportPath) return 0;

    char* path = session->commands.environmentImportPath;
    session->commands.environmentImportPath = NULL;

    if (outPath) {
        *outPath = path;
    } else {
        free(path);
    }

    return 1;
}

int sessionTakeEnvironmentClear(Session* session) {
    if (!session || !session->commands.clearEnvironmentRequested) return 0;
    session->commands.clearEnvironmentRequested = 0u;
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
    session->commands.saveImageSettings = session->editor.renderExportSettings;
    if (!path || !path[0]) return;

    session->commands.saveImagePath = stringDuplicate(path);
}

void sessionSetCurrentScenePath(Session* session, const char* path) {
    if (!session) return;

    clearOwnedString(&session->editor.currentScenePath);
    if (!path || !path[0]) return;
    session->editor.currentScenePath = stringDuplicate(path);
}

const char* sessionGetCurrentScenePath(const Session* session) {
    if (!session || !session->editor.currentScenePath || !session->editor.currentScenePath[0]) {
        return "";
    }
    return session->editor.currentScenePath;
}

void sessionSetEnvironmentTexturePath(Session* session, const char* path) {
    if (!session) return;

    clearOwnedString(&session->editor.environmentTexturePath);
    if (!path || !path[0]) return;
    session->editor.environmentTexturePath = stringDuplicate(path);
}

void sessionClearEnvironmentTexturePath(Session* session) {
    if (!session) return;
    clearOwnedString(&session->editor.environmentTexturePath);
}

const char* sessionGetEnvironmentTexturePath(const Session* session) {
    if (!session || !session->editor.environmentTexturePath || !session->editor.environmentTexturePath[0]) {
        return "";
    }
    return session->editor.environmentTexturePath;
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

int sessionAddSceneObject(Session* session, const SessionSceneObjectCreateInfo* createInfo, uint32_t* outObjectIndex) {
    if (!session) return 0;
    if (!createInfo) return 0;
    if (createInfo->parentIndex != VKRT_INVALID_INDEX && createInfo->parentIndex >= session->editor.sceneObjectCount) {
        return 0;
    }
    if (!ensureSceneObjectCapacity(session, 1u)) return 0;

    uint32_t objectIndex = session->editor.sceneObjectCount++;
    SessionSceneObject* object = &session->editor.sceneObjects[objectIndex];
    memset(object, 0, sizeof(*object));
    object->parentIndex = createInfo->parentIndex;
    object->meshIndex = createInfo->meshIndex;
    object->localScale[0] = 1.0f;
    object->localScale[1] = 1.0f;
    object->localScale[2] = 1.0f;
    if (createInfo->localPosition) {
        memcpy(object->localPosition, *createInfo->localPosition, sizeof(object->localPosition));
    }
    if (createInfo->localRotation) {
        memcpy(object->localRotation, *createInfo->localRotation, sizeof(object->localRotation));
    }
    if (createInfo->localScale) memcpy(object->localScale, *createInfo->localScale, sizeof(object->localScale));
    buildLocalTransformMatrix(object->localPosition, object->localRotation, object->localScale, object->localTransform);
    (void)snprintf(
        object->name,
        sizeof(object->name),
        "%s",
        (createInfo->name && createInfo->name[0]) ? createInfo->name : "Object"
    );
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
    (void)snprintf(session->editor.sceneObjects[objectIndex].name, VKRT_NAME_LEN, "%s", name);
    return 1;
}

int sessionSetSceneObjectMesh(Session* session, uint32_t objectIndex, uint32_t meshIndex) {
    if (!session || objectIndex >= session->editor.sceneObjectCount) return 0;
    session->editor.sceneObjects[objectIndex].meshIndex = meshIndex;
    return 1;
}

int sessionSetSceneObjectLocalTransform(
    Session* session,
    uint32_t objectIndex,
    vec3 position,
    vec3 rotation,
    vec3 scale
) {
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
    if (!session || objectIndex >= session->editor.sceneObjectCount ||
        !sceneObjectTransformMatrixValid(localTransform)) {
        return 0;
    }

    SessionSceneObject* object = &session->editor.sceneObjects[objectIndex];
    memcpy(object->localTransform, localTransform, sizeof(object->localTransform));
    decomposeImportedMeshTransform(
        object->localTransform,
        object->localPosition,
        object->localRotation,
        object->localScale
    );
    return 1;
}

int sessionSetSceneObjectLocalTransformForMesh(
    Session* session,
    uint32_t meshIndex,
    vec3 position,
    vec3 rotation,
    vec3 scale
) {
    uint32_t objectIndex = sessionFindSceneObjectForMesh(session, meshIndex);
    if (objectIndex == VKRT_INVALID_INDEX) return 0;
    return sessionSetSceneObjectLocalTransform(session, objectIndex, position, rotation, scale);
}

void sessionRemoveSceneObjectSubtree(Session* session, uint32_t objectIndex) {
    if (!session || objectIndex >= session->editor.sceneObjectCount) return;

    for (uint32_t i = session->editor.sceneObjectCount; i > 0u; i--) {
        uint32_t candidateIndex = i - 1u;
        if (!sceneObjectDescendsFrom(session, objectIndex, candidateIndex)) continue;
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
    return syncSceneObjectTransformsIterative(vkrt, session);
}

uint32_t sessionCountSceneObjectChildren(const Session* session, uint32_t objectIndex) {
    if (!session) return 0;
    uint32_t count = 0u;
    for (uint32_t i = 0; i < session->editor.sceneObjectCount; i++) {
        if (session->editor.sceneObjects[i].parentIndex == objectIndex) count++;
    }
    return count;
}

void sessionResetSceneState(Session* session) {
    clearSceneAssetState(session);
}

int sessionRegisterMeshImportBatch(Session* session, const char* sourcePath, uint32_t meshCount) {
    if (!session || !sourcePath || !sourcePath[0] || meshCount == 0u) return 0;
    if (!ensureMeshImportBatchCapacity(session, 1u) || !ensureMeshRecordCapacity(session, meshCount)) {
        return 0;
    }

    char* sourcePathCopy = stringDuplicate(sourcePath);
    if (!sourcePathCopy) return 0;

    uint32_t batchListIndex = session->editor.meshImportBatchCount++;
    session->editor.meshImportPaths[batchListIndex] = sourcePathCopy;

    for (uint32_t i = 0; i < meshCount; i++) {
        session->editor.meshRecords[session->editor.meshRecordCount + i] = (SessionMeshRecord){
            .importBatchIndex = batchListIndex,
            .importLocalIndex = i,
        };
    }
    session->editor.meshRecordCount += meshCount;
    return 1;
}

int sessionAppendImportedTextureRecords(Session* session, uint32_t textureCount) {
    if (!session) return 0;
    if (textureCount == 0u) return 1;
    if (!ensureTextureRecordCapacity(session, textureCount)) return 0;

    uint32_t baseIndex = session->editor.textureRecordCount;
    for (uint32_t i = 0; i < textureCount; i++) {
        session->editor.textureRecords[baseIndex + i] = (SessionTextureRecord){0};
    }
    session->editor.textureRecordCount += textureCount;
    return 1;
}

int sessionAppendStandaloneTextureRecord(Session* session, const char* sourcePath, uint32_t colorSpace) {
    if (!session || !sourcePath || !sourcePath[0]) return 0;
    if (!ensureTextureRecordCapacity(session, 1u)) return 0;

    char* sourcePathCopy = stringDuplicate(sourcePath);
    if (!sourcePathCopy) return 0;

    uint32_t textureIndex = session->editor.textureRecordCount++;
    session->editor.textureRecords[textureIndex] = (SessionTextureRecord){
        .sourcePath = sourcePathCopy,
        .colorSpace = colorSpace,
    };
    return 1;
}

void sessionTruncateTextureRecords(Session* session, uint32_t textureCount) {
    if (!session || textureCount >= session->editor.textureRecordCount) return;

    for (uint32_t i = textureCount; i < session->editor.textureRecordCount; i++) {
        clearTextureRecord(&session->editor.textureRecords[i]);
    }
    session->editor.textureRecordCount = textureCount;
}

void sessionRemoveMeshRecord(Session* session, uint32_t meshIndex) {
    if (!session || meshIndex >= session->editor.meshRecordCount) return;

    uint32_t lastIndex = session->editor.meshRecordCount - 1u;
    if (meshIndex != lastIndex) {
        memmove(
            &session->editor.meshRecords[meshIndex],
            &session->editor.meshRecords[meshIndex + 1u],
            (size_t)(lastIndex - meshIndex) * sizeof(SessionMeshRecord)
        );
    }
    session->editor.meshRecordCount = lastIndex;
}

const char* sessionGetMeshImportPath(const Session* session, uint32_t batchIndex) {
    if (!session || batchIndex >= session->editor.meshImportBatchCount) return NULL;
    return session->editor.meshImportPaths[batchIndex];
}

uint32_t sessionGetMeshRecordCount(const Session* session) {
    return session ? session->editor.meshRecordCount : 0u;
}

const SessionMeshRecord* sessionGetMeshRecord(const Session* session, uint32_t meshIndex) {
    if (!session || meshIndex >= session->editor.meshRecordCount) return NULL;
    return &session->editor.meshRecords[meshIndex];
}

uint32_t sessionGetTextureRecordCount(const Session* session) {
    return session ? session->editor.textureRecordCount : 0u;
}

const SessionTextureRecord* sessionGetTextureRecord(const Session* session, uint32_t textureIndex) {
    if (!session || textureIndex >= session->editor.textureRecordCount) return NULL;
    return &session->editor.textureRecords[textureIndex];
}

int sessionTakeRenderSave(Session* session, char** outPath, VKRT_RenderExportSettings* outSettings) {
    if (!session || !session->commands.saveImagePath) return 0;

    char* path = session->commands.saveImagePath;
    session->commands.saveImagePath = NULL;
    if (outSettings) *outSettings = session->commands.saveImageSettings;
    if (outPath) {
        *outPath = path;
    } else {
        free(path);
    }
    return 1;
}

void sessionQueueRenderStart(Session* session, const SessionRenderSettings* settings) {
    if (!session) return;

    SessionRenderSettings nextSettings = {0};
    if (settings) nextSettings = *settings;
    if (nextSettings.width == 0) nextSettings.width = 1;
    if (nextSettings.height == 0) nextSettings.height = 1;

    session->commands.renderCommand = SESSION_RENDER_COMMAND_START;
    session->commands.pendingRenderJob = nextSettings;
    session->commands.pendingRenderJob.denoiseEnabled = nextSettings.denoiseEnabled ? 1u : 0u;
}

void sessionQueueRenderContinue(Session* session, const SessionRenderSettings* settings) {
    if (!session) return;

    SessionRenderSettings nextSettings = {0};
    if (settings) nextSettings = *settings;

    session->commands.renderCommand = SESSION_RENDER_COMMAND_CONTINUE;
    session->commands.pendingRenderJob = nextSettings;
    session->commands.pendingRenderJob.denoiseEnabled = nextSettings.denoiseEnabled ? 1u : 0u;
}

void sessionQueueRenderSetDenoise(Session* session, uint8_t enabled) {
    if (!session) return;
    session->commands.renderCommand = SESSION_RENDER_COMMAND_SET_DENOISE;
    session->commands.pendingRenderJob.denoiseEnabled = enabled ? 1u : 0u;
}

void sessionQueueRenderStopSampling(Session* session, uint8_t denoiseEnabled) {
    if (!session) return;
    session->commands.renderCommand = SESSION_RENDER_COMMAND_STOP_SAMPLING;
    session->commands.pendingRenderJob.denoiseEnabled = denoiseEnabled ? 1u : 0u;
}

void sessionQueueRenderStop(Session* session) {
    if (!session) return;
    session->commands.renderCommand = SESSION_RENDER_COMMAND_STOP;
}

void sessionQueueRenderDenoise(Session* session) {
    if (!session) return;
    session->commands.renderCommand = SESSION_RENDER_COMMAND_DENOISE;
    session->commands.pendingRenderJob.denoiseEnabled = 1u;
}

void sessionQueueRenderResetAccumulation(Session* session) {
    if (!session) return;
    session->commands.renderCommand = SESSION_RENDER_COMMAND_RESET_ACCUMULATION;
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
