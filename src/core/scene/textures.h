#pragma once

#include "vkrt_internal.h"

#include <stddef.h>
#include <stdint.h>

static inline uint32_t materialTextureTexcoordSetShift(uint32_t textureSlot) {
    return textureSlot * 8u;
}

static inline void setIdentityMaterialTextureTransform(float4 outTransform) {
    if (!outTransform) return;
    outTransform[0] = 1.0f;
    outTransform[1] = 1.0f;
    outTransform[2] = 0.0f;
    outTransform[3] = 0.0f;
}

static inline int vkrtTextureColorSpaceValid(uint32_t colorSpace) {
    return colorSpace == VKRT_TEXTURE_COLOR_SPACE_SRGB || colorSpace == VKRT_TEXTURE_COLOR_SPACE_LINEAR;
}

static inline int vkrtTextureFormatValid(uint32_t format) {
    return format < VKRT_TEXTURE_FORMAT_COUNT;
}

static inline int vkrtTextureFormatAcceptsSRGBInput(uint32_t format) {
    return format == VKRT_TEXTURE_FORMAT_RGBA8_UNORM;
}

static inline int vkrtTextureFormatCompatibleWithColorSpace(uint32_t format, uint32_t colorSpace) {
    if (!vkrtTextureFormatValid(format) || !vkrtTextureColorSpaceValid(colorSpace)) return 0;
    if (colorSpace == VKRT_TEXTURE_COLOR_SPACE_LINEAR) return 1;
    return vkrtTextureFormatAcceptsSRGBInput(format);
}

static inline uint32_t vkrtTextureFormatBytesPerChannel(uint32_t format) {
    switch (format) {
        case VKRT_TEXTURE_FORMAT_RGBA8_UNORM:
            return 1u;
        case VKRT_TEXTURE_FORMAT_RGBA16_UNORM:
        case VKRT_TEXTURE_FORMAT_RGBA16_SFLOAT:
            return 2u;
        case VKRT_TEXTURE_FORMAT_RGBA32_SFLOAT:
            return 4u;
        default:
            return 0u;
    }
}

static inline int vkrtTryComputeTextureByteSize(uint32_t width, uint32_t height, uint32_t format, size_t* outBytes) {
    if (!outBytes || width == 0u || height == 0u) return 0;
    uint32_t bytesPerChannel = vkrtTextureFormatBytesPerChannel(format);
    if (bytesPerChannel == 0u) return 0;

    size_t rowBytes = (size_t)width * 4u * (size_t)bytesPerChannel;
    if ((size_t)height > SIZE_MAX / rowBytes) return 0;

    *outBytes = rowBytes * (size_t)height;
    return 1;
}

static inline int vkrtTextureUploadValid(const VKRT_TextureUpload* upload) {
    if (!upload || !upload->pixels || upload->width == 0u || upload->height == 0u) return 0;
    return vkrtTextureFormatCompatibleWithColorSpace(upload->format, upload->colorSpace);
}

const SceneTexture* vkrtGetSceneTexture(const VKRT* vkrt, uint32_t textureIndex);
uint32_t vkrtCountTextureUsers(const VKRT* vkrt, uint32_t textureIndex);
void vkrtAdjustMaterialTextureUseCounts(VKRT* vkrt, const Material* material, int delta);

VKRT_Result vkrtSceneAddTextureFromFile(
    VKRT* vkrt,
    const char* path,
    const char* name,
    uint32_t colorSpace,
    uint32_t* outTextureIndex
);
VKRT_Result vkrtSceneAddTextureFromPixels(VKRT* vkrt, const VKRT_TextureUpload* upload, uint32_t* outTextureIndex);
VKRT_Result vkrtSceneAddTexturesBatch(
    VKRT* vkrt,
    const VKRT_TextureUpload* uploads,
    size_t uploadCount,
    uint32_t* outTextureIndices
);
VKRT_Result vkrtSceneRemoveTexture(VKRT* vkrt, uint32_t textureIndex);
VKRT_Result vkrtSceneSetMaterialTexture(
    VKRT* vkrt,
    uint32_t materialIndex,
    uint32_t textureSlot,
    uint32_t textureIndex
);
VKRT_Result vkrtEnsureTextureBindings(VKRT* vkrt);
void vkrtReleaseSceneTextures(VKRT* vkrt);
