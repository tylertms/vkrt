#include "textures.h"

#include "debug.h"
#include "environment.h"
#include "image.h"
#include "images.h"
#include "io.h"
#include "scene.h"
#include "state.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TextureSlotAccess {
    uint32_t textureSlot;
    uint32_t* index;
    uint32_t* wrap;
    uint32_t* texcoordSets;
    float4* transform;
    float* rotation;
} TextureSlotAccess;

static void resetMaterialTextureTexcoordSet(uint32_t* packedSets, uint32_t textureSlot) {
    if (!packedSets) return;
    *packedSets &= ~(0xffu << materialTextureTexcoordSetShift(textureSlot));
}

static TextureSlotAccess accessMaterialTextureSlot(Material* material, uint32_t textureSlot) {
    TextureSlotAccess access = {0};
    if (!material) return access;

    switch (textureSlot) {
        case VKRT_MATERIAL_TEXTURE_SLOT_BASE_COLOR:
            access.textureSlot = textureSlot;
            access.index = &material->baseColorTextureIndex;
            access.wrap = &material->baseColorTextureWrap;
            access.texcoordSets = &material->textureTexcoordSets;
            access.transform = &material->baseColorTextureTransform;
            access.rotation = &material->textureRotations[0];
            break;
        case VKRT_MATERIAL_TEXTURE_SLOT_METALLIC_ROUGHNESS:
            access.textureSlot = textureSlot;
            access.index = &material->metallicRoughnessTextureIndex;
            access.wrap = &material->metallicRoughnessTextureWrap;
            access.texcoordSets = &material->textureTexcoordSets;
            access.transform = &material->metallicRoughnessTextureTransform;
            access.rotation = &material->textureRotations[1];
            break;
        case VKRT_MATERIAL_TEXTURE_SLOT_NORMAL:
            access.textureSlot = textureSlot;
            access.index = &material->normalTextureIndex;
            access.wrap = &material->normalTextureWrap;
            access.texcoordSets = &material->textureTexcoordSets;
            access.transform = &material->normalTextureTransform;
            access.rotation = &material->textureRotations[2];
            break;
        case VKRT_MATERIAL_TEXTURE_SLOT_EMISSIVE:
            access.textureSlot = textureSlot;
            access.index = &material->emissiveTextureIndex;
            access.wrap = &material->emissiveTextureWrap;
            access.texcoordSets = &material->textureTexcoordSets;
            access.transform = &material->emissiveTextureTransform;
            access.rotation = &material->textureRotations[3];
            break;
        default:
            break;
    }

    return access;
}

static uint32_t defaultWrapMode(void) {
    return VKRT_TEXTURE_WRAP_DEFAULT;
}

static int textureSlotExpectsColor(uint32_t textureSlot) {
    return textureSlot == VKRT_MATERIAL_TEXTURE_SLOT_BASE_COLOR ||
        textureSlot == VKRT_MATERIAL_TEXTURE_SLOT_EMISSIVE;
}

static int textureCompatibleWithSlot(uint32_t textureSlot, const SceneTexture* texture) {
    if (!texture) return 0;
    if (textureSlotExpectsColor(textureSlot)) {
        return texture->colorSpace == VKRT_TEXTURE_COLOR_SPACE_SRGB ||
            texture->colorSpace == VKRT_TEXTURE_COLOR_SPACE_LINEAR;
    }
    return texture->colorSpace == VKRT_TEXTURE_COLOR_SPACE_LINEAR;
}

static void destroySceneTextureResources(VKRT* vkrt, SceneTexture* texture) {
    if (!texture) return;
    vkrtDestroyImageResources(vkrt, &texture->image, &texture->view, &texture->memory);
    texture->width = 0u;
    texture->height = 0u;
    texture->format = VKRT_TEXTURE_FORMAT_RGBA8_UNORM;
    texture->colorSpace = VKRT_TEXTURE_COLOR_SPACE_SRGB;
    texture->useCount = 0u;
    texture->name[0] = '\0';
}

static VkSamplerAddressMode textureAddressMode(uint32_t wrapMode) {
    switch (wrapMode) {
        case VKRT_TEXTURE_WRAP_CLAMP_TO_EDGE:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case VKRT_TEXTURE_WRAP_MIRRORED_REPEAT:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case VKRT_TEXTURE_WRAP_REPEAT:
        default:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static VKRT_Result ensureTextureSamplers(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrt->core.textureSamplers[0] != VK_NULL_HANDLE) return VKRT_SUCCESS;

    static const uint32_t kWrapModes[3] = {
        VKRT_TEXTURE_WRAP_REPEAT,
        VKRT_TEXTURE_WRAP_CLAMP_TO_EDGE,
        VKRT_TEXTURE_WRAP_MIRRORED_REPEAT,
    };

    for (uint32_t wrapS = 0; wrapS < 3u; wrapS++) {
        for (uint32_t wrapT = 0; wrapT < 3u; wrapT++) {
            uint32_t samplerIndex = wrapS * 3u + wrapT;
            VkSamplerCreateInfo createInfo = {0};
            createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            createInfo.magFilter = VK_FILTER_LINEAR;
            createInfo.minFilter = VK_FILTER_LINEAR;
            createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            createInfo.addressModeU = textureAddressMode(kWrapModes[wrapS]);
            createInfo.addressModeV = textureAddressMode(kWrapModes[wrapT]);
            createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            createInfo.maxLod = 0.0f;
            createInfo.maxAnisotropy = 1.0f;
            createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

            if (vkCreateSampler(vkrt->core.device, &createInfo, NULL, &vkrt->core.textureSamplers[samplerIndex]) != VK_SUCCESS) {
                for (uint32_t i = 0; i < samplerIndex; i++) {
                    if (vkrt->core.textureSamplers[i] != VK_NULL_HANDLE) {
                        vkDestroySampler(vkrt->core.device, vkrt->core.textureSamplers[i], NULL);
                        vkrt->core.textureSamplers[i] = VK_NULL_HANDLE;
                    }
                }
                return VKRT_ERROR_OPERATION_FAILED;
            }
        }
    }
    return VKRT_SUCCESS;
}

static VKRT_Result ensureTextureFallback(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrt->core.textureFallbackView != VK_NULL_HANDLE) return VKRT_SUCCESS;

    static const uint8_t kFallbackPixel[4] = {255u, 255u, 255u, 255u};
    return vkrtCreateSampledTextureImageFromData(
        vkrt,
        kFallbackPixel,
        1u,
        1u,
        VK_FORMAT_R8G8B8A8_UNORM,
        4u,
        &vkrt->core.textureFallbackImage,
        &vkrt->core.textureFallbackView,
        &vkrt->core.textureFallbackMemory
    );
}

static int queryTextureVkFormat(uint32_t format, uint32_t colorSpace, VkFormat* outFormat) {
    if (!outFormat) return 0;

    switch (format) {
        case VKRT_TEXTURE_FORMAT_RGBA8_UNORM:
            *outFormat = colorSpace == VKRT_TEXTURE_COLOR_SPACE_SRGB
                ? VK_FORMAT_R8G8B8A8_SRGB
                : VK_FORMAT_R8G8B8A8_UNORM;
            return 1;
        case VKRT_TEXTURE_FORMAT_RGBA16_UNORM:
            if (colorSpace != VKRT_TEXTURE_COLOR_SPACE_LINEAR) return 0;
            *outFormat = VK_FORMAT_R16G16B16A16_UNORM;
            return 1;
        case VKRT_TEXTURE_FORMAT_RGBA16_SFLOAT:
            if (colorSpace != VKRT_TEXTURE_COLOR_SPACE_LINEAR) return 0;
            *outFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
            return 1;
        case VKRT_TEXTURE_FORMAT_RGBA32_SFLOAT:
            if (colorSpace != VKRT_TEXTURE_COLOR_SPACE_LINEAR) return 0;
            *outFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
            return 1;
        default:
            return 0;
    }
}

static int uploadSceneTexture(
    VKRT* vkrt,
    const char* name,
    const void* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t colorSpace,
    SceneTexture* outTexture
) {
    if (!name || !pixels || !outTexture) return 0;

    *outTexture = (SceneTexture){0};
    VkFormat vkFormat = VK_FORMAT_UNDEFINED;
    size_t byteSize = 0u;

    if (!queryTextureVkFormat(format, colorSpace, &vkFormat)) {
        LOG_ERROR("Unsupported texture upload format/color space combination (%u, %u)", format, colorSpace);
        return 0;
    }
    if (!vkrtTryComputeTextureByteSize(width, height, format, &byteSize)) {
        return 0;
    }

    if (vkrtCreateSampledTextureImageFromData(
        vkrt,
        pixels,
        width,
        height,
        vkFormat,
        (VkDeviceSize)byteSize,
        &outTexture->image,
        &outTexture->view,
        &outTexture->memory
    ) != VKRT_SUCCESS) {
        return 0;
    }

    outTexture->width = width;
    outTexture->height = height;
    outTexture->format = format;
    outTexture->colorSpace = colorSpace;
    snprintf(outTexture->name, sizeof(outTexture->name), "%s", name[0] ? name : "Texture");
    return 1;
}

static VKRT_Result appendSceneTexture(
    VKRT* vkrt,
    SceneTexture* texture,
    uint32_t* outTextureIndex
) {
    if (!vkrt || !texture) return VKRT_ERROR_INVALID_ARGUMENT;
    if (outTextureIndex) *outTextureIndex = VKRT_INVALID_INDEX;
    if (vkrt->core.textureCount >= VKRT_MAX_BINDLESS_TEXTURES) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint32_t textureIndex = vkrt->core.textureCount;
    SceneTexture* resized = (SceneTexture*)realloc(
        vkrt->core.textures,
        (size_t)(textureIndex + 1u) * sizeof(SceneTexture)
    );
    if (!resized) {
        destroySceneTextureResources(vkrt, texture);
        return VKRT_ERROR_OUT_OF_MEMORY;
    }

    vkrt->core.textures = resized;
    vkrt->core.textures[textureIndex] = *texture;
    vkrt->core.textureCount = textureIndex + 1u;
    if (outTextureIndex) *outTextureIndex = textureIndex;
    return VKRT_SUCCESS;
}

VKRT_Result vkrtEnsureTextureBindings(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT_Result result = ensureTextureSamplers(vkrt);
    if (result != VKRT_SUCCESS) return result;
    return ensureTextureFallback(vkrt);
}

void vkrtReleaseSceneTextures(VKRT* vkrt) {
    if (!vkrt) return;

    if (vkrt->core.textures) {
        for (uint32_t i = 0; i < vkrt->core.textureCount; i++) {
            destroySceneTextureResources(vkrt, &vkrt->core.textures[i]);
        }
        free(vkrt->core.textures);
    }
    vkrt->core.textures = NULL;
    vkrt->core.textureCount = 0u;

    vkrtDestroyImageResources(
        vkrt,
        &vkrt->core.textureFallbackImage,
        &vkrt->core.textureFallbackView,
        &vkrt->core.textureFallbackMemory
    );
    if (vkrt->core.device != VK_NULL_HANDLE) {
        for (uint32_t i = 0; i < VKRT_TEXTURE_SAMPLER_VARIANT_COUNT; i++) {
            if (vkrt->core.textureSamplers[i] != VK_NULL_HANDLE) {
                vkDestroySampler(vkrt->core.device, vkrt->core.textureSamplers[i], NULL);
                vkrt->core.textureSamplers[i] = VK_NULL_HANDLE;
            }
        }
    }
}

const SceneTexture* vkrtGetSceneTexture(const VKRT* vkrt, uint32_t textureIndex) {
    if (!vkrt || textureIndex >= vkrt->core.textureCount || !vkrt->core.textures) return NULL;
    return &vkrt->core.textures[textureIndex];
}

uint32_t vkrtCountTextureUsers(const VKRT* vkrt, uint32_t textureIndex) {
    if (!vkrt || textureIndex >= vkrt->core.textureCount || !vkrt->core.textures) return 0u;
    return vkrt->core.textures[textureIndex].useCount;
}

void vkrtAdjustMaterialTextureUseCounts(VKRT* vkrt, const Material* material, int delta) {
    if (!vkrt || !material || !vkrt->core.textures) return;

    const uint32_t indices[] = {
        material->baseColorTextureIndex,
        material->metallicRoughnessTextureIndex,
        material->normalTextureIndex,
        material->emissiveTextureIndex,
    };
    for (uint32_t i = 0; i < 4u; i++) {
        if (indices[i] != VKRT_INVALID_INDEX && indices[i] < vkrt->core.textureCount) {
            int32_t newCount = (int32_t)vkrt->core.textures[indices[i]].useCount + delta;
            vkrt->core.textures[indices[i]].useCount = newCount > 0 ? (uint32_t)newCount : 0u;
        }
    }
}

VKRT_Result vkrtSceneAddTextureFromPixels(
    VKRT* vkrt,
    const char* name,
    const void* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t colorSpace,
    uint32_t* outTextureIndex
) {
    if (outTextureIndex) *outTextureIndex = VKRT_INVALID_INDEX;
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (!pixels || width == 0u || height == 0u) return VKRT_ERROR_INVALID_ARGUMENT;
    if (!vkrtTextureFormatCompatibleWithColorSpace(format, colorSpace)) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    size_t byteSize = 0u;
    if (!vkrtTryComputeTextureByteSize(width, height, format, &byteSize)) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }
    (void)byteSize;

    VKRT_Result result = vkrtWaitForAllInFlightFrames(vkrt);
    if (result != VKRT_SUCCESS) return result;

    result = vkrtEnsureTextureBindings(vkrt);
    if (result != VKRT_SUCCESS) return result;

    SceneTexture texture = {0};
    if (!uploadSceneTexture(vkrt, name ? name : "Texture", pixels, width, height, format, colorSpace, &texture)) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    result = appendSceneTexture(vkrt, &texture, outTextureIndex);
    if (result != VKRT_SUCCESS) return result;

    vkrtMarkTextureResourcesDirty(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result vkrtSceneAddTexturesBatch(
    VKRT* vkrt,
    const VKRT_TextureUpload* uploads,
    size_t uploadCount,
    uint32_t* outTextureIndices
) {
    if (!vkrt || !uploads || uploadCount == 0u) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if ((size_t)vkrt->core.textureCount + uploadCount > VKRT_MAX_BINDLESS_TEXTURES) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    for (size_t i = 0; i < uploadCount; i++) {
        const VKRT_TextureUpload* upload = &uploads[i];
        if (!vkrtTextureUploadValid(upload)) return VKRT_ERROR_INVALID_ARGUMENT;

        size_t byteSize = 0u;
        if (!vkrtTryComputeTextureByteSize(upload->width, upload->height, upload->format, &byteSize)) {
            return VKRT_ERROR_INVALID_ARGUMENT;
        }
        (void)byteSize;
    }

    VKRT_Result result = vkrtWaitForAllInFlightFrames(vkrt);
    if (result != VKRT_SUCCESS) return result;

    result = vkrtEnsureTextureBindings(vkrt);
    if (result != VKRT_SUCCESS) return result;

    uint32_t addedCount = 0u;
    for (size_t i = 0; i < uploadCount; i++) {
        SceneTexture texture = {0};
        if (!uploadSceneTexture(
            vkrt,
            uploads[i].name ? uploads[i].name : "Texture",
            uploads[i].pixels,
            uploads[i].width,
            uploads[i].height,
            uploads[i].format,
            uploads[i].colorSpace,
            &texture
        )) {
            result = VKRT_ERROR_OPERATION_FAILED;
        } else {
            result = appendSceneTexture(vkrt, &texture, outTextureIndices ? &outTextureIndices[i] : NULL);
        }
        if (result == VKRT_SUCCESS) {
            addedCount++;
            continue;
        }

        for (uint32_t j = 0; j < addedCount; j++) {
            destroySceneTextureResources(vkrt, &vkrt->core.textures[vkrt->core.textureCount - 1u - j]);
        }
        vkrt->core.textureCount -= addedCount;
        if (vkrt->core.textureCount == 0u) {
            free(vkrt->core.textures);
            vkrt->core.textures = NULL;
        } else {
            SceneTexture* shrunk = (SceneTexture*)realloc(
                vkrt->core.textures,
                (size_t)vkrt->core.textureCount * sizeof(SceneTexture)
            );
            if (shrunk) {
                vkrt->core.textures = shrunk;
            }
        }
        return result;
    }

    vkrtMarkTextureResourcesDirty(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result vkrtSceneAddTextureFromFile(
    VKRT* vkrt,
    const char* path,
    const char* name,
    uint32_t colorSpace,
    uint32_t* outTextureIndex
) {
    if (outTextureIndex) *outTextureIndex = VKRT_INVALID_INDEX;
    if (!path || !path[0]) return VKRT_ERROR_INVALID_ARGUMENT;

    char resolvedPath[VKRT_PATH_MAX];
    if (resolveExistingPath(path, resolvedPath, sizeof(resolvedPath)) != 0) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    VKRT_LoadedImage image = {0};
    if (!vkrtLoadImageFromFile(resolvedPath, colorSpace, &image)) {
        return VKRT_ERROR_OPERATION_FAILED;
    }

    const char* textureName = name && name[0] ? name : pathBasename(resolvedPath);
    VKRT_Result result = vkrtSceneAddTextureFromPixels(
        vkrt,
        textureName,
        image.pixels,
        image.width,
        image.height,
        image.format,
        image.colorSpace,
        outTextureIndex
    );
    vkrtFreeLoadedImage(&image);
    return result;
}

VKRT_Result vkrtSceneRemoveTexture(VKRT* vkrt, uint32_t textureIndex) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (textureIndex >= vkrt->core.textureCount) return VKRT_ERROR_INVALID_ARGUMENT;
    if (vkrtCountTextureUsers(vkrt, textureIndex) != 0u) return VKRT_ERROR_OPERATION_FAILED;

    VKRT_Result result = vkrtWaitForAllInFlightFrames(vkrt);
    if (result != VKRT_SUCCESS) return result;

    destroySceneTextureResources(vkrt, &vkrt->core.textures[textureIndex]);

    uint32_t textureCount = vkrt->core.textureCount;
    if (textureIndex + 1u < textureCount) {
        memmove(
            &vkrt->core.textures[textureIndex],
            &vkrt->core.textures[textureIndex + 1u],
            (size_t)(textureCount - textureIndex - 1u) * sizeof(SceneTexture)
        );
    }

    uint32_t newCount = textureCount - 1u;
    if (newCount == 0u) {
        free(vkrt->core.textures);
        vkrt->core.textures = NULL;
    } else {
        SceneTexture* shrunk = (SceneTexture*)realloc(vkrt->core.textures, (size_t)newCount * sizeof(SceneTexture));
        if (shrunk) {
            vkrt->core.textures = shrunk;
        }
    }
    vkrt->core.textureCount = newCount;

    for (uint32_t materialIndex = 0; materialIndex < vkrt->core.materialCount; materialIndex++) {
        Material* material = &vkrt->core.materials[materialIndex].material;
        uint32_t* indices[] = {
            &material->baseColorTextureIndex,
            &material->metallicRoughnessTextureIndex,
            &material->normalTextureIndex,
            &material->emissiveTextureIndex,
        };

        for (uint32_t i = 0; i < VKRT_ARRAY_COUNT(indices); i++) {
            if (*indices[i] != VKRT_INVALID_INDEX && *indices[i] > textureIndex) {
                (*indices[i])--;
            }
        }
    }

    vkrtRemapEnvironmentTextureIndexAfterRemoval(vkrt, textureIndex);
    vkrtMarkTextureResourcesDirty(vkrt);
    vkrtMarkMaterialResourcesDirty(vkrt);
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result vkrtSceneSetMaterialTexture(
    VKRT* vkrt,
    uint32_t materialIndex,
    uint32_t textureSlot,
    uint32_t textureIndex
) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (materialIndex >= vkrt->core.materialCount) return VKRT_ERROR_INVALID_ARGUMENT;

    Material* material = &vkrt->core.materials[materialIndex].material;
    TextureSlotAccess access = accessMaterialTextureSlot(material, textureSlot);
    if (!access.index || !access.wrap || !access.texcoordSets || !access.transform || !access.rotation) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    if (textureIndex != VKRT_INVALID_INDEX) {
        const SceneTexture* texture = vkrtGetSceneTexture(vkrt, textureIndex);
        if (!texture) return VKRT_ERROR_INVALID_ARGUMENT;
        if (!textureCompatibleWithSlot(textureSlot, texture)) {
            return VKRT_ERROR_INVALID_ARGUMENT;
        }
    }

    if (*access.index == textureIndex) return VKRT_SUCCESS;

    uint32_t previousTextureIndex = *access.index;
    if (previousTextureIndex != VKRT_INVALID_INDEX && previousTextureIndex < vkrt->core.textureCount) {
        int32_t count = (int32_t)vkrt->core.textures[previousTextureIndex].useCount - 1;
        vkrt->core.textures[previousTextureIndex].useCount = count > 0 ? (uint32_t)count : 0u;
    }
    if (textureIndex != VKRT_INVALID_INDEX && textureIndex < vkrt->core.textureCount) {
        vkrt->core.textures[textureIndex].useCount++;
    }
    *access.index = textureIndex;
    *access.wrap = defaultWrapMode();
    resetMaterialTextureTexcoordSet(access.texcoordSets, access.textureSlot);
    setIdentityMaterialTextureTransform(*access.transform);
    *access.rotation = 0.0f;
    vkrtMarkMaterialResourcesDirty(vkrt);
    resetSceneData(vkrt);
    return vkrtReleaseTextureIfUnused(vkrt, previousTextureIndex);
}
