#include "environment.h"

#include "scene.h"
#include "textures.h"

static VKRT_Result replaceEnvironmentTexture(VKRT* vkrt, uint32_t nextTextureIndex) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    uint32_t previousTextureIndex = vkrt->sceneSettings.environmentTextureIndex;
    if (nextTextureIndex != VKRT_INVALID_INDEX) {
        const SceneTexture* texture = vkrtGetSceneTexture(vkrt, nextTextureIndex);
        if (!texture || texture->colorSpace != VKRT_TEXTURE_COLOR_SPACE_LINEAR) {
            return VKRT_ERROR_INVALID_ARGUMENT;
        }
    }

    if (previousTextureIndex == nextTextureIndex) return VKRT_SUCCESS;

    vkrt->sceneSettings.environmentTextureIndex = VKRT_INVALID_INDEX;
    if (previousTextureIndex != VKRT_INVALID_INDEX &&
        previousTextureIndex < vkrt->core.textureCount &&
        vkrtCountTextureUsers(vkrt, previousTextureIndex) == 0u) {
        VKRT_Result result = vkrtSceneRemoveTexture(vkrt, previousTextureIndex);
        if (result != VKRT_SUCCESS) {
            vkrt->sceneSettings.environmentTextureIndex = previousTextureIndex;
            return result;
        }
        if (nextTextureIndex != VKRT_INVALID_INDEX && previousTextureIndex < nextTextureIndex) {
            nextTextureIndex--;
        }
    }

    vkrt->sceneSettings.environmentTextureIndex = nextTextureIndex;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result vkrtSceneSetEnvironmentTextureFromFile(VKRT* vkrt, const char* path) {
    if (!path || !path[0]) return VKRT_ERROR_INVALID_ARGUMENT;

    uint32_t textureIndex = VKRT_INVALID_INDEX;
    VKRT_Result result = vkrtSceneAddTextureFromFile(
        vkrt,
        path,
        NULL,
        VKRT_TEXTURE_COLOR_SPACE_LINEAR,
        &textureIndex
    );
    if (result != VKRT_SUCCESS) return result;

    return replaceEnvironmentTexture(vkrt, textureIndex);
}

VKRT_Result vkrtSceneClearEnvironmentTexture(VKRT* vkrt) {
    return replaceEnvironmentTexture(vkrt, VKRT_INVALID_INDEX);
}

void vkrtRemapEnvironmentTextureIndexAfterRemoval(VKRT* vkrt, uint32_t removedTextureIndex) {
    if (!vkrt) return;

    uint32_t textureIndex = vkrt->sceneSettings.environmentTextureIndex;
    if (textureIndex == VKRT_INVALID_INDEX) return;

    if (textureIndex == removedTextureIndex) {
        vkrt->sceneSettings.environmentTextureIndex = VKRT_INVALID_INDEX;
    } else if (textureIndex > removedTextureIndex) {
        vkrt->sceneSettings.environmentTextureIndex = textureIndex - 1u;
    }
}
