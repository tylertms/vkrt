#pragma once

#include "vkrt_internal.h"

VKRT_Result vkrtSceneSetEnvironmentTextureFromFile(VKRT* vkrt, const char* path);
VKRT_Result vkrtSceneClearEnvironmentTexture(VKRT* vkrt);
void vkrtRemapEnvironmentTextureIndexAfterRemoval(VKRT* vkrt, uint32_t removedTextureIndex);
