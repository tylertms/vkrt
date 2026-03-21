#include "textures.h"

VKRT_Result VKRT_addTextureFromPixels(
    VKRT* vkrt,
    const char* name,
    const void* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t colorSpace,
    uint32_t* outTextureIndex
) {
    if (outTextureIndex) *outTextureIndex = VKRT_INVALID_INDEX;
    if (!vkrt || !pixels || width == 0u || height == 0u || !vkrtTextureColorSpaceValid(colorSpace)) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }
    return vkrtSceneAddTextureFromPixels(vkrt, name, pixels, width, height, colorSpace, outTextureIndex);
}

VKRT_Result VKRT_addTextureFromFile(
    VKRT* vkrt,
    const char* path,
    const char* name,
    uint32_t colorSpace,
    uint32_t* outTextureIndex
) {
    if (outTextureIndex) *outTextureIndex = VKRT_INVALID_INDEX;
    if (!vkrt || !path || !path[0] || !vkrtTextureColorSpaceValid(colorSpace)) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }
    return vkrtSceneAddTextureFromFile(vkrt, path, name, colorSpace, outTextureIndex);
}

VKRT_Result VKRT_removeTexture(VKRT* vkrt, uint32_t textureIndex) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    return vkrtSceneRemoveTexture(vkrt, textureIndex);
}

VKRT_Result VKRT_addTexturesBatch(
    VKRT* vkrt,
    const VKRT_TextureUpload* uploads,
    size_t uploadCount,
    uint32_t* outTextureIndices
) {
    if (!vkrt || !uploads || uploadCount == 0u) return VKRT_ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < uploadCount; i++) {
        if (!vkrtTextureUploadValid(&uploads[i])) return VKRT_ERROR_INVALID_ARGUMENT;
    }
    return vkrtSceneAddTexturesBatch(vkrt, uploads, uploadCount, outTextureIndices);
}

VKRT_Result VKRT_setMaterialTexture(
    VKRT* vkrt,
    uint32_t materialIndex,
    uint32_t textureSlot,
    uint32_t textureIndex
) {
    if (!vkrt || textureSlot >= VKRT_MATERIAL_TEXTURE_SLOT_COUNT) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }
    return vkrtSceneSetMaterialTexture(vkrt, materialIndex, textureSlot, textureIndex);
}
