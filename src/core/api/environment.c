#include "scene/environment.h"

#include "vkrt_types.h"

VKRT_Result VKRT_setEnvironmentTextureFromFile(VKRT* vkrt, const char* path) {
    if (!vkrt || !path || !path[0]) return VKRT_ERROR_INVALID_ARGUMENT;
    return vkrtSceneSetEnvironmentTextureFromFile(vkrt, path);
}

VKRT_Result VKRT_clearEnvironmentTexture(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    return vkrtSceneClearEnvironmentTexture(vkrt);
}
