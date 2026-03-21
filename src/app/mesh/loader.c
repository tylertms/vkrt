#include "loader.h"

#include "debug.h"
#include "image.h"
#include "io.h"
#include "platform.h"
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kGeneratedMeshNameCapacity = 256 };
static const size_t kMeshImportMaxPrimitiveBytes = 1024u * 1024u * 1024u;
static const uint32_t kMeshImportTextureDecodeThreadCount = 6u;
static const float kAlphaBlendMaskCutoff = 1.0f / 255.0f;

typedef struct MeshImportFeatureReport {
    int unsupportedMaterialTextures;
    int unsupportedTextureTransforms;
    int advancedMaterials;
    int skinning;
    int morphTargets;
    int cameras;
    int lights;
    int animations;
    int gpuInstancing;
    int dracoCompression;
} MeshImportFeatureReport;

typedef struct ImportedTextureReference {
    const cgltf_image* image;
    const cgltf_texture* texture;
    uint32_t colorSpace;
} ImportedTextureReference;

typedef struct TextureDecodeThreadContext {
    const char* resolvedPath;
    const ImportedTextureReference* references;
    TextureImportEntry* outputs;
    uint32_t startIndex;
    uint32_t endIndex;
    int failed;
} TextureDecodeThreadContext;

static int textureViewUsesUnsupportedTransform(const cgltf_texture_view* textureView);
static Material buildMaterial(
    const cgltf_material* sourceMaterial,
    const char* resolvedPath,
    MeshImportData* importData,
    const cgltf_image** cachedImages,
    uint32_t* cachedColorSpaces,
    uint32_t* cachedTextureIndices,
    uint32_t* inoutCachedTextureCount
);

static void releaseImportEntry(MeshImportEntry* entry) {
    if (!entry) return;
    free(entry->name);
    free(entry->vertices);
    free(entry->indices);
    memset(entry, 0, sizeof(*entry));
}

static void releaseImportMaterial(MaterialImportEntry* material) {
    if (!material) return;
    free(material->name);
    memset(material, 0, sizeof(*material));
}

static void releaseImportNode(NodeImportEntry* node) {
    if (!node) return;
    free(node->name);
    memset(node, 0, sizeof(*node));
}

static void releaseImportTexture(TextureImportEntry* texture) {
    if (!texture) return;
    free(texture->name);
    free(texture->rgba8);
    memset(texture, 0, sizeof(*texture));
}

void meshReleaseImportData(MeshImportData* importData) {
    if (!importData) return;

    for (uint32_t i = 0; i < importData->count; i++) {
        releaseImportEntry(&importData->entries[i]);
    }

    free(importData->entries);
    importData->entries = NULL;
    importData->count = 0;
    importData->entryCapacity = 0;

    for (uint32_t i = 0; i < importData->nodeCount; i++) {
        releaseImportNode(&importData->nodes[i]);
    }

    free(importData->nodes);
    importData->nodes = NULL;
    importData->nodeCount = 0;
    importData->nodeCapacity = 0;

    for (uint32_t i = 0; i < importData->materialCount; i++) {
        releaseImportMaterial(&importData->materials[i]);
    }

    free(importData->materials);
    importData->materials = NULL;
    importData->materialCount = 0;

    for (uint32_t i = 0; i < importData->textureCount; i++) {
        releaseImportTexture(&importData->textures[i]);
    }

    free(importData->textures);
    importData->textures = NULL;
    importData->textureCount = 0;
    importData->textureCapacity = 0;
}

static int ensureImportEntryCapacity(MeshImportData* importData, uint32_t additionalCount) {
    if (!importData) return -1;
    if (additionalCount == 0u) return 0;
    if (importData->count > UINT32_MAX - additionalCount) return -1;

    uint32_t requiredCount = importData->count + additionalCount;
    if (requiredCount <= importData->entryCapacity) return 0;

    uint32_t nextCapacity = importData->entryCapacity > 0u ? importData->entryCapacity : 16u;
    while (nextCapacity < requiredCount) {
        if (nextCapacity > UINT32_MAX / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }

    MeshImportEntry* resized = (MeshImportEntry*)realloc(
        importData->entries,
        (size_t)nextCapacity * sizeof(MeshImportEntry)
    );
    if (!resized) return -1;

    importData->entries = resized;
    importData->entryCapacity = nextCapacity;
    return 0;
}

static int ensureImportTextureCapacity(MeshImportData* importData, uint32_t additionalCount) {
    if (!importData) return -1;
    if (additionalCount == 0u) return 0;
    if (importData->textureCount > UINT32_MAX - additionalCount) return -1;

    uint32_t requiredCount = importData->textureCount + additionalCount;
    if (requiredCount <= importData->textureCapacity) return 0;

    uint32_t nextCapacity = importData->textureCapacity > 0u ? importData->textureCapacity : 8u;
    while (nextCapacity < requiredCount) {
        if (nextCapacity > UINT32_MAX / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }

    TextureImportEntry* resized = (TextureImportEntry*)realloc(
        importData->textures,
        (size_t)nextCapacity * sizeof(TextureImportEntry)
    );
    if (!resized) return -1;

    importData->textures = resized;
    importData->textureCapacity = nextCapacity;
    return 0;
}

static int ensureImportNodeCapacity(MeshImportData* importData, uint32_t additionalCount) {
    if (!importData) return -1;
    if (additionalCount == 0u) return 0;
    if (importData->nodeCount > UINT32_MAX - additionalCount) return -1;

    uint32_t requiredCount = importData->nodeCount + additionalCount;
    if (requiredCount <= importData->nodeCapacity) return 0;

    uint32_t nextCapacity = importData->nodeCapacity > 0u ? importData->nodeCapacity : 16u;
    while (nextCapacity < requiredCount) {
        if (nextCapacity > UINT32_MAX / 2u) {
            nextCapacity = requiredCount;
            break;
        }
        nextCapacity *= 2u;
    }

    NodeImportEntry* resized = (NodeImportEntry*)realloc(
        importData->nodes,
        (size_t)nextCapacity * sizeof(NodeImportEntry)
    );
    if (!resized) return -1;

    importData->nodes = resized;
    importData->nodeCapacity = nextCapacity;
    return 0;
}

static int appendImportEntry(MeshImportData* importData, MeshImportEntry* entry) {
    if (!importData || !entry) return -1;
    if (ensureImportEntryCapacity(importData, 1u) != 0) return -1;

    importData->entries[importData->count] = *entry;
    importData->count++;
    memset(entry, 0, sizeof(*entry));
    return 0;
}

static int appendImportTexture(MeshImportData* importData, TextureImportEntry* texture) {
    if (!importData || !texture) return -1;
    if (ensureImportTextureCapacity(importData, 1u) != 0) return -1;

    importData->textures[importData->textureCount] = *texture;
    importData->textureCount++;
    memset(texture, 0, sizeof(*texture));
    return 0;
}

static int appendImportNode(MeshImportData* importData, NodeImportEntry* node, uint32_t* outNodeIndex) {
    if (!importData || !node) return -1;
    if (ensureImportNodeCapacity(importData, 1u) != 0) return -1;

    uint32_t nodeIndex = importData->nodeCount;
    importData->nodes[nodeIndex] = *node;
    importData->nodeCount++;
    if (outNodeIndex) *outNodeIndex = nodeIndex;
    memset(node, 0, sizeof(*node));
    return 0;
}

static int textureReferenceEqual(
    const ImportedTextureReference* reference,
    const cgltf_image* image,
    uint32_t colorSpace
) {
    return reference &&
        reference->image == image &&
        reference->colorSpace == colorSpace;
}

static int appendImportedTextureReference(
    ImportedTextureReference* references,
    uint32_t* inoutReferenceCount,
    const cgltf_texture_view* textureView,
    uint32_t colorSpace
) {
    if (!references || !inoutReferenceCount) {
        return 0;
    }
    if (!textureView || !textureView->texture || !textureView->texture->image) {
        return 1;
    }
    if (textureViewUsesUnsupportedTransform(textureView)) {
        return 1;
    }

    const cgltf_image* image = textureView->texture->image;
    for (uint32_t i = 0; i < *inoutReferenceCount; i++) {
        if (textureReferenceEqual(&references[i], image, colorSpace)) {
            return 1;
        }
    }

    references[*inoutReferenceCount] = (ImportedTextureReference){
        .image = image,
        .texture = textureView->texture,
        .colorSpace = colorSpace,
    };
    (*inoutReferenceCount)++;
    return 1;
}

static int collectImportedTextureReferences(
    const cgltf_data* data,
    ImportedTextureReference* references,
    uint32_t* outReferenceCount
) {
    if (!data || !references || !outReferenceCount) return 0;
    *outReferenceCount = 0u;

    for (cgltf_size materialIndex = 0; materialIndex < data->materials_count; materialIndex++) {
        const cgltf_material* material = &data->materials[materialIndex];
        if (!appendImportedTextureReference(
            references,
            outReferenceCount,
            &material->pbr_metallic_roughness.base_color_texture,
            VKRT_TEXTURE_COLOR_SPACE_SRGB
        )) {
            return 0;
        }
        if (!appendImportedTextureReference(
            references,
            outReferenceCount,
            &material->pbr_metallic_roughness.metallic_roughness_texture,
            VKRT_TEXTURE_COLOR_SPACE_LINEAR
        )) {
            return 0;
        }
        if (!appendImportedTextureReference(
            references,
            outReferenceCount,
            &material->normal_texture,
            VKRT_TEXTURE_COLOR_SPACE_LINEAR
        )) {
            return 0;
        }
        if (!appendImportedTextureReference(
            references,
            outReferenceCount,
            &material->emissive_texture,
            VKRT_TEXTURE_COLOR_SPACE_SRGB
        )) {
            return 0;
        }
    }

    return 1;
}

static int copyParentDirectory(const char* path, char outDirectory[VKRT_PATH_MAX]) {
    if (!path || !outDirectory) return 0;
    if (snprintf(outDirectory, VKRT_PATH_MAX, "%s", path) >= VKRT_PATH_MAX) return 0;

    char* slash = strrchr(outDirectory, '/');
    char* backslash = strrchr(outDirectory, '\\');
    char* separator = slash > backslash ? slash : backslash;
    if (!separator) {
        outDirectory[0] = '.';
        outDirectory[1] = '\0';
        return 1;
    }

    if (separator == outDirectory) {
        separator[1] = '\0';
    } else {
        *separator = '\0';
    }
    return 1;
}

static uint32_t packTextureWrapModes(cgltf_wrap_mode wrapS, cgltf_wrap_mode wrapT) {
    uint32_t s = wrapS != 0 ? (uint32_t)wrapS : VKRT_TEXTURE_WRAP_REPEAT;
    uint32_t t = wrapT != 0 ? (uint32_t)wrapT : VKRT_TEXTURE_WRAP_REPEAT;
    return s | (t << 16u);
}

static uint32_t materialTextureTexcoordSetShift(uint32_t textureSlot) {
    return textureSlot * 8u;
}

static void setMaterialTextureTexcoordSet(Material* material, uint32_t textureSlot, uint32_t texcoordSet) {
    if (!material) return;

    uint32_t shift = materialTextureTexcoordSetShift(textureSlot);
    uint32_t mask = 0xffu << shift;
    material->textureTexcoordSets =
        (material->textureTexcoordSets & ~mask) | ((texcoordSet & 0xffu) << shift);
}

static uint32_t queryTextureViewTexcoordSet(const cgltf_texture_view* textureView) {
    if (!textureView) return 0u;
    if (textureView->has_transform && textureView->transform.has_texcoord && textureView->transform.texcoord >= 0) {
        return (uint32_t)textureView->transform.texcoord;
    }
    return textureView->texcoord >= 0 ? (uint32_t)textureView->texcoord : 0u;
}

static void copyTextureTransform(float4 outTransform, float* outRotation, const cgltf_texture_view* textureView) {
    if (!outTransform || !outRotation) return;
    outTransform[0] = 1.0f;
    outTransform[1] = 1.0f;
    outTransform[2] = 0.0f;
    outTransform[3] = 0.0f;
    *outRotation = 0.0f;
    if (!textureView) return;

    if (textureView->has_transform) {
        outTransform[0] = textureView->transform.scale[0];
        outTransform[1] = textureView->transform.scale[1];
        outTransform[2] = textureView->transform.offset[0];
        outTransform[3] = textureView->transform.offset[1];
        *outRotation = textureView->transform.rotation;
    }
}

static int resolveTextureImagePath(
    const char* resolvedPath,
    const cgltf_image* image,
    char outPath[VKRT_PATH_MAX]
) {
    if (!resolvedPath || !image || !image->uri || !image->uri[0] || !outPath) return 0;
    if (strstr(image->uri, "data:") == image->uri) return 0;

    char decodedUri[VKRT_PATH_MAX];
    if (snprintf(decodedUri, sizeof(decodedUri), "%s", image->uri) >= (int)sizeof(decodedUri)) {
        return 0;
    }
    cgltf_decode_uri(decodedUri);

    if (resolveExistingPath(decodedUri, outPath, VKRT_PATH_MAX) == 0) {
        return 1;
    }

    char sceneDirectory[VKRT_PATH_MAX];
    if (!copyParentDirectory(resolvedPath, sceneDirectory)) return 0;

    char combinedPath[VKRT_PATH_MAX];
    if (snprintf(combinedPath, sizeof(combinedPath), "%s/%s", sceneDirectory, decodedUri) >= (int)sizeof(combinedPath)) {
        return 0;
    }

    return resolveExistingPath(combinedPath, outPath, VKRT_PATH_MAX) == 0;
}

static int decodeTextureImage(
    const char* resolvedPath,
    const cgltf_image* image,
    VKRT_LoadedImage* outImage
) {
    if (!resolvedPath || !image || !outImage) return 0;
    *outImage = (VKRT_LoadedImage){0};

    if (image->uri && image->uri[0] && strstr(image->uri, "data:") != image->uri) {
        char imagePath[VKRT_PATH_MAX];
        if (!resolveTextureImagePath(resolvedPath, image, imagePath)) {
            return 0;
        }
        return vkrtLoadImageFromFile(imagePath, outImage);
    }

    if (image->buffer_view &&
        image->buffer_view->buffer &&
        image->buffer_view->buffer->data &&
        image->buffer_view->size <= image->buffer_view->buffer->size &&
        image->buffer_view->offset <= image->buffer_view->buffer->size - image->buffer_view->size) {
        const uint8_t* bytes = (const uint8_t*)image->buffer_view->buffer->data + image->buffer_view->offset;
        return vkrtLoadImageFromMemory(bytes, image->buffer_view->size, image->mime_type, outImage);
    }

    return 0;
}

static const char* queryImportedTextureName(const cgltf_image* image, const cgltf_texture* texture) {
    if (image && image->name && image->name[0]) {
        return image->name;
    }
    if (texture && texture->name && texture->name[0]) {
        return texture->name;
    }
    return "Texture";
}

static int buildImportedTextureEntry(
    const char* resolvedPath,
    const cgltf_image* image,
    const cgltf_texture* texture,
    uint32_t colorSpace,
    TextureImportEntry* outEntry
) {
    if (!resolvedPath || !image || !outEntry) return 0;
    *outEntry = (TextureImportEntry){0};

    VKRT_LoadedImage decoded = {0};
    if (!decodeTextureImage(resolvedPath, image, &decoded)) {
        return 0;
    }

    char* duplicatedName = stringDuplicate(queryImportedTextureName(image, texture));
    if (!duplicatedName) {
        vkrtFreeLoadedImage(&decoded);
        return 0;
    }

    *outEntry = (TextureImportEntry){
        .name = duplicatedName,
        .rgba8 = decoded.pixels,
        .width = decoded.width,
        .height = decoded.height,
        .colorSpace = colorSpace,
    };
    return 1;
}

static int decodeTextureReferenceRange(void* userData) {
    TextureDecodeThreadContext* context = (TextureDecodeThreadContext*)userData;
    if (!context) return -1;

    for (uint32_t i = context->startIndex; i < context->endIndex; i++) {
        const ImportedTextureReference* reference = &context->references[i];
        if (!buildImportedTextureEntry(
            context->resolvedPath,
            reference->image,
            reference->texture,
            reference->colorSpace,
            &context->outputs[i]
        )) {
            context->failed = 1;
            return -1;
        }
    }

    return 0;
}

static void releaseTextureImportEntries(TextureImportEntry* textures, uint32_t textureCount) {
    if (!textures) return;
    for (uint32_t i = 0; i < textureCount; i++) {
        releaseImportTexture(&textures[i]);
    }
}

static int decodeImportedTextureReferences(
    MeshImportData* importData,
    const char* resolvedPath,
    const ImportedTextureReference* references,
    uint32_t referenceCount,
    const cgltf_image** outCachedImages,
    uint32_t* outCachedColorSpaces,
    uint32_t* outCachedTextureIndices
) {
    if (!importData || !resolvedPath || !references || !outCachedImages || !outCachedColorSpaces || !outCachedTextureIndices) {
        return 0;
    }
    if (referenceCount == 0u) return 1;

    TextureImportEntry* decodedTextures = (TextureImportEntry*)calloc(referenceCount, sizeof(*decodedTextures));
    if (!decodedTextures) return 0;

    uint32_t threadCount = referenceCount < kMeshImportTextureDecodeThreadCount
        ? referenceCount
        : kMeshImportTextureDecodeThreadCount;
    TextureDecodeThreadContext* contexts = (TextureDecodeThreadContext*)calloc(threadCount, sizeof(*contexts));
    VKRT_Thread* threads = (VKRT_Thread*)calloc(threadCount, sizeof(*threads));
    if (!contexts || !threads) {
        free(contexts);
        free(threads);
        free(decodedTextures);
        return 0;
    }

    uint32_t chunkSize = (referenceCount + threadCount - 1u) / threadCount;
    uint32_t launchedThreads = 0u;
    int failed = 0;
    for (uint32_t threadIndex = 0; threadIndex < threadCount; threadIndex++) {
        uint32_t startIndex = threadIndex * chunkSize;
        uint32_t endIndex = startIndex + chunkSize;
        if (startIndex >= referenceCount) break;
        if (endIndex > referenceCount) endIndex = referenceCount;

        contexts[threadIndex] = (TextureDecodeThreadContext){
            .resolvedPath = resolvedPath,
            .references = references,
            .outputs = decodedTextures,
            .startIndex = startIndex,
            .endIndex = endIndex,
            .failed = 0,
        };

        if (vkrtThreadCreate(&threads[threadIndex], decodeTextureReferenceRange, &contexts[threadIndex]) != VKRT_THREAD_SUCCESS) {
            failed = 1;
            launchedThreads = threadIndex;
            break;
        }
        launchedThreads = threadIndex + 1u;
    }

    for (uint32_t threadIndex = 0; threadIndex < launchedThreads; threadIndex++) {
        int threadResult = 0;
        if (vkrtThreadJoin(threads[threadIndex], &threadResult) != VKRT_THREAD_SUCCESS || threadResult != 0 || contexts[threadIndex].failed) {
            failed = 1;
        }
    }

    if (!failed) {
        for (uint32_t i = 0; i < referenceCount; i++) {
            if (appendImportTexture(importData, &decodedTextures[i]) != 0) {
                failed = 1;
                break;
            }

            outCachedImages[i] = references[i].image;
            outCachedColorSpaces[i] = references[i].colorSpace;
            outCachedTextureIndices[i] = i;
        }
    }

    if (failed) {
        releaseTextureImportEntries(decodedTextures, referenceCount);
    }

    free(contexts);
    free(threads);
    free(decodedTextures);
    return failed ? 0 : 1;
}

static int registerImportedTexture(
    MeshImportData* importData,
    const char* resolvedPath,
    const cgltf_texture_view* textureView,
    uint32_t colorSpace,
    const cgltf_image** cachedImages,
    uint32_t* cachedColorSpaces,
    uint32_t* cachedTextureIndices,
    uint32_t* inoutCachedTextureCount,
    uint32_t* outTextureIndex
) {
    if (outTextureIndex) *outTextureIndex = VKRT_INVALID_INDEX;
    if (!importData || !textureView || !textureView->texture || !textureView->texture->image ||
        !cachedImages || !cachedColorSpaces || !cachedTextureIndices || !inoutCachedTextureCount) {
        return 0;
    }
    if (textureViewUsesUnsupportedTransform(textureView)) {
        return 0;
    }

    const cgltf_image* image = textureView->texture->image;
    for (uint32_t i = 0; i < *inoutCachedTextureCount; i++) {
        if (cachedImages[i] == image && cachedColorSpaces[i] == colorSpace) {
            if (outTextureIndex) *outTextureIndex = cachedTextureIndices[i];
            return 1;
        }
    }

    TextureImportEntry texture = {0};
    if (!buildImportedTextureEntry(resolvedPath, image, textureView->texture, colorSpace, &texture)) {
        return 0;
    }
    if (appendImportTexture(importData, &texture) != 0) {
        releaseImportTexture(&texture);
        return 0;
    }

    uint32_t textureIndex = importData->textureCount - 1u;
    cachedImages[*inoutCachedTextureCount] = image;
    cachedTextureIndices[*inoutCachedTextureCount] = textureIndex;
    cachedColorSpaces[*inoutCachedTextureCount] = colorSpace;
    (*inoutCachedTextureCount)++;

    if (outTextureIndex) *outTextureIndex = textureIndex;
    return 1;
}

static int populateImportMaterials(const cgltf_data* data, MeshImportData* importData, const char* resolvedPath) {
    if (!data || !importData) return -1;
    if (data->materials_count == 0) return 0;
    if (data->materials_count > (cgltf_size)VKRT_INVALID_INDEX) return -1;

    MaterialImportEntry* materials = (MaterialImportEntry*)calloc(
        (size_t)data->materials_count,
        sizeof(MaterialImportEntry)
    );
    if (!materials) return -1;

    uint32_t maxTextureRefs = (uint32_t)data->materials_count * VKRT_MATERIAL_TEXTURE_SLOT_COUNT;
    ImportedTextureReference* references = maxTextureRefs > 0
        ? (ImportedTextureReference*)calloc(maxTextureRefs, sizeof(*references))
        : NULL;
    const cgltf_image** cachedImages = maxTextureRefs > 0
        ? (const cgltf_image**)calloc(maxTextureRefs, sizeof(*cachedImages))
        : NULL;
    uint32_t* cachedColorSpaces = maxTextureRefs > 0
        ? (uint32_t*)calloc(maxTextureRefs, sizeof(*cachedColorSpaces))
        : NULL;
    uint32_t* cachedTextureIndices = maxTextureRefs > 0
        ? (uint32_t*)calloc(maxTextureRefs, sizeof(*cachedTextureIndices))
        : NULL;
    uint32_t cachedTextureCount = 0u;
    if (maxTextureRefs > 0 && (!references || !cachedImages || !cachedColorSpaces || !cachedTextureIndices)) {
        free(materials);
        free(references);
        free(cachedImages);
        free(cachedColorSpaces);
        free(cachedTextureIndices);
        return -1;
    }

    if (maxTextureRefs > 0) {
        if (!collectImportedTextureReferences(data, references, &cachedTextureCount) ||
            !decodeImportedTextureReferences(
                importData,
                resolvedPath,
                references,
                cachedTextureCount,
                cachedImages,
                cachedColorSpaces,
                cachedTextureIndices
            )) {
            free(materials);
            free(references);
            free(cachedImages);
            free(cachedColorSpaces);
            free(cachedTextureIndices);
            return -1;
        }
    }

    for (cgltf_size materialIndex = 0; materialIndex < data->materials_count; materialIndex++) {
        const cgltf_material* sourceMaterial = &data->materials[materialIndex];
        char generatedName[VKRT_NAME_LEN];
        if (sourceMaterial->name && sourceMaterial->name[0]) {
            snprintf(generatedName, sizeof(generatedName), "%s", sourceMaterial->name);
        } else {
            snprintf(generatedName, sizeof(generatedName), "Material %zu", materialIndex);
        }

        materials[materialIndex].material = buildMaterial(
            sourceMaterial,
            resolvedPath,
            importData,
            cachedImages,
            cachedColorSpaces,
            cachedTextureIndices,
            &cachedTextureCount
        );
        materials[materialIndex].name = stringDuplicate(generatedName);
        if (!materials[materialIndex].name) {
            for (cgltf_size i = 0; i <= materialIndex; i++) {
                releaseImportMaterial(&materials[i]);
            }
            free(materials);
            free(references);
            free(cachedImages);
            free(cachedColorSpaces);
            free(cachedTextureIndices);
            return -1;
        }
    }

    importData->materials = materials;
    importData->materialCount = (uint32_t)data->materials_count;
    free(references);
    free(cachedImages);
    free(cachedColorSpaces);
    free(cachedTextureIndices);
    return 0;
}

static const cgltf_accessor* findAttributeAccessor(
    const cgltf_primitive* primitive,
    cgltf_attribute_type type,
    cgltf_size attributeIndex
) {
    if (!primitive) return NULL;

    for (cgltf_size i = 0; i < primitive->attributes_count; i++) {
        if (primitive->attributes[i].type == type &&
            primitive->attributes[i].index >= 0 &&
            (cgltf_size)primitive->attributes[i].index == attributeIndex) {
            return primitive->attributes[i].data;
        }
    }

    return NULL;
}

static void buildEntryName(
    char* outName,
    size_t outNameSize,
    const cgltf_node* node,
    const cgltf_mesh* mesh,
    cgltf_size primitiveIndex
) {
    if (!outName || outNameSize == 0) return;

    const char* baseName = "mesh";
    if (node && node->name && node->name[0]) {
        baseName = node->name;
    } else if (mesh && mesh->name && mesh->name[0]) {
        baseName = mesh->name;
    }

    if (mesh && mesh->primitives_count > 1) {
        snprintf(outName, outNameSize, "%s_%zu", baseName, primitiveIndex);
    } else {
        snprintf(outName, outNameSize, "%s", baseName);
    }
}

static int checkedMultiplySize(size_t count, size_t stride, size_t* outBytes) {
    if (!outBytes) return 0;
    *outBytes = 0;
    if (count == 0 || stride == 0) return 1;
    if (count > SIZE_MAX / stride) return 0;
    *outBytes = count * stride;
    return 1;
}

static int checkedAddSize(size_t lhs, size_t rhs, size_t* outBytes) {
    if (!outBytes) return 0;
    *outBytes = 0;
    if (lhs > SIZE_MAX - rhs) return 0;
    *outBytes = lhs + rhs;
    return 1;
}

static int validatePrimitiveAllocationFootprint(size_t vertexCount, size_t indexCount, int needsTexcoords) {
    if (vertexCount == 0 || vertexCount > (size_t)VKRT_INVALID_INDEX) {
        LOG_ERROR("glTF mesh import rejected an invalid vertex count: %zu", vertexCount);
        return 0;
    }
    if (indexCount == 0 || indexCount > (size_t)VKRT_INVALID_INDEX) {
        LOG_ERROR("glTF mesh import rejected an invalid index count: %zu", indexCount);
        return 0;
    }

    size_t vertexBytes = 0;
    size_t indexBytes = 0;
    size_t texcoordBytes = 0;
    size_t totalBytes = 0;
    if (!checkedMultiplySize(vertexCount, sizeof(Vertex), &vertexBytes) ||
        !checkedMultiplySize(indexCount, sizeof(uint32_t), &indexBytes) ||
        !checkedAddSize(vertexBytes, indexBytes, &totalBytes)) {
        LOG_ERROR("glTF mesh import rejected a primitive with overflowing buffer sizes");
        return 0;
    }

    if (needsTexcoords) {
        if (!checkedMultiplySize(vertexCount, sizeof(float) * 2u, &texcoordBytes) ||
            !checkedAddSize(totalBytes, texcoordBytes, &totalBytes)) {
            LOG_ERROR("glTF mesh import rejected a primitive with overflowing texcoord buffer sizes");
            return 0;
        }
    }

    if (totalBytes > kMeshImportMaxPrimitiveBytes) {
        LOG_ERROR(
            "glTF mesh import rejected a primitive requiring %zu bytes of host staging (limit: %zu)",
            totalBytes,
            kMeshImportMaxPrimitiveBytes
        );
        return 0;
    }

    return 1;
}

static int materialUsesTextureView(const cgltf_texture_view* textureView) {
    return textureView && textureView->texture != NULL;
}

static int textureViewUsesUnsupportedTransform(const cgltf_texture_view* textureView) {
    if (!textureView) return 0;
    return queryTextureViewTexcoordSet(textureView) > 1u;
}

static int materialUsesUnsupportedTextureTransforms(const cgltf_material* material) {
    if (!material) return 0;

    return textureViewUsesUnsupportedTransform(&material->pbr_metallic_roughness.base_color_texture) ||
        textureViewUsesUnsupportedTransform(&material->pbr_metallic_roughness.metallic_roughness_texture) ||
        textureViewUsesUnsupportedTransform(&material->normal_texture) ||
        textureViewUsesUnsupportedTransform(&material->emissive_texture);
}

static int materialUsesUnsupportedTextures(const cgltf_material* material) {
    if (!material) return 0;

    return materialUsesTextureView(&material->occlusion_texture) ||
        materialUsesTextureView(&material->clearcoat.clearcoat_texture) ||
        materialUsesTextureView(&material->clearcoat.clearcoat_roughness_texture) ||
        materialUsesTextureView(&material->clearcoat.clearcoat_normal_texture) ||
        materialUsesTextureView(&material->specular.specular_texture) ||
        materialUsesTextureView(&material->specular.specular_color_texture) ||
        materialUsesTextureView(&material->transmission.transmission_texture) ||
        materialUsesTextureView(&material->volume.thickness_texture) ||
        materialUsesTextureView(&material->sheen.sheen_color_texture) ||
        materialUsesTextureView(&material->sheen.sheen_roughness_texture) ||
        materialUsesTextureView(&material->iridescence.iridescence_texture) ||
        materialUsesTextureView(&material->iridescence.iridescence_thickness_texture) ||
        materialUsesTextureView(&material->diffuse_transmission.diffuse_transmission_texture) ||
        materialUsesTextureView(&material->diffuse_transmission.diffuse_transmission_color_texture) ||
        materialUsesTextureView(&material->anisotropy.anisotropy_texture);
}

static int materialUsesUnsupportedVolume(const cgltf_material* material) {
    if (!material || !material->has_volume) return 0;
    return material->volume.thickness_factor > 0.0f;
}

static int materialUsesUnsupportedModels(const cgltf_material* material) {
    if (!material) return 0;

    return material->has_pbr_specular_glossiness ||
        materialUsesUnsupportedVolume(material) ||
        material->has_iridescence ||
        material->has_diffuse_transmission ||
        material->has_anisotropy ||
        material->has_dispersion ||
        material->unlit;
}

static void collectIgnoredImportFeatures(const cgltf_data* data, MeshImportFeatureReport* outReport) {
    if (!data || !outReport) return;

    outReport->animations = data->animations_count > 0;
    outReport->cameras = data->cameras_count > 0;
    outReport->lights = data->lights_count > 0;
    outReport->skinning = data->skins_count > 0;

    for (cgltf_size materialIndex = 0; materialIndex < data->materials_count; materialIndex++) {
        const cgltf_material* material = &data->materials[materialIndex];
        outReport->unsupportedMaterialTextures |= materialUsesUnsupportedTextures(material);
        outReport->unsupportedTextureTransforms |= materialUsesUnsupportedTextureTransforms(material);
        outReport->advancedMaterials |= materialUsesUnsupportedModels(material);
    }

    for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; nodeIndex++) {
        const cgltf_node* node = &data->nodes[nodeIndex];
        outReport->gpuInstancing |= node->has_mesh_gpu_instancing;
        outReport->skinning |= node->skin != NULL;
    }

    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; meshIndex++) {
        const cgltf_mesh* mesh = &data->meshes[meshIndex];
        outReport->morphTargets |= mesh->weights_count > 0;

        for (cgltf_size primitiveIndex = 0; primitiveIndex < mesh->primitives_count; primitiveIndex++) {
            const cgltf_primitive* primitive = &mesh->primitives[primitiveIndex];
            outReport->morphTargets |= primitive->targets_count > 0;
            outReport->dracoCompression |= primitive->has_draco_mesh_compression;

            for (cgltf_size attributeIndex = 0; attributeIndex < primitive->attributes_count; attributeIndex++) {
                const cgltf_attribute* attribute = &primitive->attributes[attributeIndex];
                outReport->skinning |=
                    attribute->type == cgltf_attribute_type_joints || attribute->type == cgltf_attribute_type_weights;
            }
        }
    }
}

static void appendIgnoredFeature(char* buffer, size_t bufferSize, const char* label) {
    if (!buffer || bufferSize == 0 || !label || !label[0]) return;

    size_t currentLength = strlen(buffer);
    if (currentLength >= bufferSize - 1u) return;

    const char* separator = currentLength > 0 ? ", " : "";
    (void)snprintf(buffer + currentLength, bufferSize - currentLength, "%s%s", separator, label);
}

static void logIgnoredImportFeatures(const char* resolvedPath, const cgltf_data* data) {
    if (!resolvedPath || !resolvedPath[0] || !data) return;

    MeshImportFeatureReport report = {0};
    collectIgnoredImportFeatures(data, &report);

    char ignoredFeatures[384] = {0};
    if (report.unsupportedMaterialTextures) {
        appendIgnoredFeature(ignoredFeatures, sizeof(ignoredFeatures), "unsupported material textures/extensions");
    }
    if (report.unsupportedTextureTransforms) {
        appendIgnoredFeature(ignoredFeatures, sizeof(ignoredFeatures), "texture transforms using texcoord sets above 1");
    }
    if (report.advancedMaterials) appendIgnoredFeature(ignoredFeatures, sizeof(ignoredFeatures), "advanced material models");
    if (report.skinning) appendIgnoredFeature(ignoredFeatures, sizeof(ignoredFeatures), "skinning data");
    if (report.morphTargets) appendIgnoredFeature(ignoredFeatures, sizeof(ignoredFeatures), "morph targets");
    if (report.cameras) appendIgnoredFeature(ignoredFeatures, sizeof(ignoredFeatures), "cameras");
    if (report.lights) appendIgnoredFeature(ignoredFeatures, sizeof(ignoredFeatures), "lights");
    if (report.animations) appendIgnoredFeature(ignoredFeatures, sizeof(ignoredFeatures), "animations");
    if (report.gpuInstancing) appendIgnoredFeature(ignoredFeatures, sizeof(ignoredFeatures), "GPU instancing");
    if (report.dracoCompression) appendIgnoredFeature(ignoredFeatures, sizeof(ignoredFeatures), "Draco-compressed primitives");

    if (ignoredFeatures[0]) {
        LOG_INFO("glTF mesh import ignored %s in '%s'", ignoredFeatures, resolvedPath);
    }
}

static float max3(float a, float b, float c) {
    float value = a;
    if (b > value) value = b;
    if (c > value) value = c;
    return value;
}

static void copyFloat3ToFloat4(float out[4], const float in[3], float w) {
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = w;
}

static void loadVertexPosition3(const Vertex* vertices, uint32_t index, vec3 out) {
    out[0] = vertices[index].position[0];
    out[1] = vertices[index].position[1];
    out[2] = vertices[index].position[2];
}

static void loadVertexNormal3(const Vertex* vertices, uint32_t index, vec3 out) {
    out[0] = vertices[index].normal[0];
    out[1] = vertices[index].normal[1];
    out[2] = vertices[index].normal[2];
}

static int loadTriangleIndices(size_t vertexCount, const uint32_t* indices, size_t indexOffset, uint32_t out[3]) {
    uint32_t i0 = indices[indexOffset + 0];
    uint32_t i1 = indices[indexOffset + 1];
    uint32_t i2 = indices[indexOffset + 2];
    if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
        return 0;
    }

    out[0] = i0;
    out[1] = i1;
    out[2] = i2;
    return 1;
}

static float triangleNormalAlignment(const Vertex* vertices, size_t vertexCount, const uint32_t* indices, size_t indexOffset) {
    uint32_t triangle[3];
    if (!loadTriangleIndices(vertexCount, indices, indexOffset, triangle)) {
        return 0.0f;
    }

    vec3 p0, p1, p2;
    loadVertexPosition3(vertices, triangle[0], p0);
    loadVertexPosition3(vertices, triangle[1], p1);
    loadVertexPosition3(vertices, triangle[2], p2);

    vec3 edge1 = GLM_VEC3_ZERO_INIT;
    vec3 edge2 = GLM_VEC3_ZERO_INIT;
    glm_vec3_sub(p1, p0, edge1);
    glm_vec3_sub(p2, p0, edge2);

    vec3 faceNormal = GLM_VEC3_ZERO_INIT;
    glm_vec3_cross(edge1, edge2, faceNormal);
    if (glm_vec3_norm2(faceNormal) <= 1e-12f) {
        return 0.0f;
    }

    vec3 averagedNormal = GLM_VEC3_ZERO_INIT;
    averagedNormal[0] = vertices[triangle[0]].normal[0] + vertices[triangle[1]].normal[0] + vertices[triangle[2]].normal[0];
    averagedNormal[1] = vertices[triangle[0]].normal[1] + vertices[triangle[1]].normal[1] + vertices[triangle[2]].normal[1];
    averagedNormal[2] = vertices[triangle[0]].normal[2] + vertices[triangle[1]].normal[2] + vertices[triangle[2]].normal[2];
    if (glm_vec3_norm2(averagedNormal) <= 1e-12f) {
        return 0.0f;
    }

    return glm_vec3_dot(faceNormal, averagedNormal);
}

static int orthonormalizeTangentFrame(const float normal[3], const float tangentIn[3], float handedness, float outTangent[4]) {
    vec3 n = {normal[0], normal[1], normal[2]};
    vec3 tangent = {tangentIn[0], tangentIn[1], tangentIn[2]};
    if (glm_vec3_norm2(n) <= 1e-12f || glm_vec3_norm2(tangent) <= 1e-12f) {
        return 0;
    }

    glm_vec3_normalize(n);
    float tangentDotNormal = glm_vec3_dot(tangent, n);
    vec3 projectedNormal = {
        n[0] * tangentDotNormal,
        n[1] * tangentDotNormal,
        n[2] * tangentDotNormal,
    };
    glm_vec3_sub(tangent, projectedNormal, tangent);
    if (glm_vec3_norm2(tangent) <= 1e-12f) {
        return 0;
    }

    glm_vec3_normalize(tangent);
    outTangent[0] = tangent[0];
    outTangent[1] = tangent[1];
    outTangent[2] = tangent[2];
    outTangent[3] = handedness < 0.0f ? -1.0f : 1.0f;
    return 1;
}

static void buildFallbackTangent(const float normal[3], float outTangent[4]) {
    vec3 n = {normal[0], normal[1], normal[2]};
    if (glm_vec3_norm2(n) <= 1e-12f) {
        n[2] = 1.0f;
    } else {
        glm_vec3_normalize(n);
    }

    vec3 up = {0.0f, 0.0f, 1.0f};
    if (fabsf(n[2]) > 0.999f) {
        up[0] = 1.0f;
        up[2] = 0.0f;
    }

    vec3 tangent = {0.0f, 0.0f, 0.0f};
    glm_vec3_cross(up, n, tangent);
    if (glm_vec3_norm2(tangent) <= 1e-12f) {
        tangent[0] = 1.0f;
        tangent[1] = 0.0f;
        tangent[2] = 0.0f;
    } else {
        glm_vec3_normalize(tangent);
    }

    outTangent[0] = tangent[0];
    outTangent[1] = tangent[1];
    outTangent[2] = tangent[2];
    outTangent[3] = 1.0f;
}

static void finalizeTangents(Vertex* vertices, size_t vertexCount) {
    if (!vertices) return;

    for (size_t vertexIndex = 0; vertexIndex < vertexCount; vertexIndex++) {
        float handedness = vertices[vertexIndex].tangent[3] < 0.0f ? -1.0f : 1.0f;
        if (!orthonormalizeTangentFrame(
            vertices[vertexIndex].normal,
            vertices[vertexIndex].tangent,
            handedness,
            vertices[vertexIndex].tangent
        )) {
            buildFallbackTangent(vertices[vertexIndex].normal, vertices[vertexIndex].tangent);
        }
    }
}

static Material buildMaterial(
    const cgltf_material* sourceMaterial,
    const char* resolvedPath,
    MeshImportData* importData,
    const cgltf_image** cachedImages,
    uint32_t* cachedColorSpaces,
    uint32_t* cachedTextureIndices,
    uint32_t* inoutCachedTextureCount
) {
    Material material = VKRT_materialDefault();
    if (!sourceMaterial) {
        return material;
    }

    if (sourceMaterial->has_pbr_metallic_roughness) {
        material.baseColor[0] = sourceMaterial->pbr_metallic_roughness.base_color_factor[0];
        material.baseColor[1] = sourceMaterial->pbr_metallic_roughness.base_color_factor[1];
        material.baseColor[2] = sourceMaterial->pbr_metallic_roughness.base_color_factor[2];
        material.opacity = sourceMaterial->pbr_metallic_roughness.base_color_factor[3];
        material.metallic = sourceMaterial->pbr_metallic_roughness.metallic_factor;
        material.roughness = sourceMaterial->pbr_metallic_roughness.roughness_factor;

        uint32_t baseColorTextureIndex = VKRT_INVALID_INDEX;
        if (registerImportedTexture(
            importData,
            resolvedPath,
            &sourceMaterial->pbr_metallic_roughness.base_color_texture,
            VKRT_TEXTURE_COLOR_SPACE_SRGB,
            cachedImages,
            cachedColorSpaces,
            cachedTextureIndices,
            inoutCachedTextureCount,
            &baseColorTextureIndex
        )) {
            material.baseColorTextureIndex = baseColorTextureIndex;
            material.baseColorTextureWrap = packTextureWrapModes(
                sourceMaterial->pbr_metallic_roughness.base_color_texture.texture->sampler
                    ? sourceMaterial->pbr_metallic_roughness.base_color_texture.texture->sampler->wrap_s
                    : cgltf_wrap_mode_repeat,
                sourceMaterial->pbr_metallic_roughness.base_color_texture.texture->sampler
                    ? sourceMaterial->pbr_metallic_roughness.base_color_texture.texture->sampler->wrap_t
                    : cgltf_wrap_mode_repeat
            );
            setMaterialTextureTexcoordSet(
                &material,
                VKRT_MATERIAL_TEXTURE_SLOT_BASE_COLOR,
                queryTextureViewTexcoordSet(&sourceMaterial->pbr_metallic_roughness.base_color_texture)
            );
            copyTextureTransform(
                material.baseColorTextureTransform,
                &material.textureRotations[VKRT_MATERIAL_TEXTURE_SLOT_BASE_COLOR],
                &sourceMaterial->pbr_metallic_roughness.base_color_texture
            );
        }

        uint32_t metallicRoughnessTextureIndex = VKRT_INVALID_INDEX;
        if (registerImportedTexture(
            importData,
            resolvedPath,
            &sourceMaterial->pbr_metallic_roughness.metallic_roughness_texture,
            VKRT_TEXTURE_COLOR_SPACE_LINEAR,
            cachedImages,
            cachedColorSpaces,
            cachedTextureIndices,
            inoutCachedTextureCount,
            &metallicRoughnessTextureIndex
        )) {
            material.metallicRoughnessTextureIndex = metallicRoughnessTextureIndex;
            material.metallicRoughnessTextureWrap = packTextureWrapModes(
                sourceMaterial->pbr_metallic_roughness.metallic_roughness_texture.texture->sampler
                    ? sourceMaterial->pbr_metallic_roughness.metallic_roughness_texture.texture->sampler->wrap_s
                    : cgltf_wrap_mode_repeat,
                sourceMaterial->pbr_metallic_roughness.metallic_roughness_texture.texture->sampler
                    ? sourceMaterial->pbr_metallic_roughness.metallic_roughness_texture.texture->sampler->wrap_t
                    : cgltf_wrap_mode_repeat
            );
            setMaterialTextureTexcoordSet(
                &material,
                VKRT_MATERIAL_TEXTURE_SLOT_METALLIC_ROUGHNESS,
                queryTextureViewTexcoordSet(&sourceMaterial->pbr_metallic_roughness.metallic_roughness_texture)
            );
            copyTextureTransform(
                material.metallicRoughnessTextureTransform,
                &material.textureRotations[VKRT_MATERIAL_TEXTURE_SLOT_METALLIC_ROUGHNESS],
                &sourceMaterial->pbr_metallic_roughness.metallic_roughness_texture
            );
        }
    }

    if (sourceMaterial->has_specular) {
        material.specular = sourceMaterial->specular.specular_factor;
    }

    if (sourceMaterial->has_ior) {
        material.ior = sourceMaterial->ior.ior;
    }

    if (sourceMaterial->has_transmission) {
        material.transmission = sourceMaterial->transmission.transmission_factor;
    }

    if (sourceMaterial->has_volume) {
        material.attenuationColor[0] = sourceMaterial->volume.attenuation_color[0];
        material.attenuationColor[1] = sourceMaterial->volume.attenuation_color[1];
        material.attenuationColor[2] = sourceMaterial->volume.attenuation_color[2];
        material.absorptionCoefficient = sourceMaterial->volume.attenuation_distance > 0.0f &&
            isfinite(sourceMaterial->volume.attenuation_distance)
            ? 1.0f / sourceMaterial->volume.attenuation_distance
            : 0.0f;
    }

    if (sourceMaterial->has_clearcoat) {
        material.clearcoat = sourceMaterial->clearcoat.clearcoat_factor;
        material.clearcoatGloss = 1.0f - sourceMaterial->clearcoat.clearcoat_roughness_factor;
    }

    if (sourceMaterial->has_sheen) {
        vec3 sheenColor = {
            sourceMaterial->sheen.sheen_color_factor[0],
            sourceMaterial->sheen.sheen_color_factor[1],
            sourceMaterial->sheen.sheen_color_factor[2]
        };
        float sheenWeight = max3(sheenColor[0], sheenColor[1], sheenColor[2]);
        if (sheenWeight > 0.0f) {
            material.sheenTintWeight[0] = sheenColor[0] / sheenWeight;
            material.sheenTintWeight[1] = sheenColor[1] / sheenWeight;
            material.sheenTintWeight[2] = sheenColor[2] / sheenWeight;
            material.sheenTintWeight[3] = sheenWeight;
        } else {
            material.sheenTintWeight[0] = 1.0f;
            material.sheenTintWeight[1] = 1.0f;
            material.sheenTintWeight[2] = 1.0f;
            material.sheenTintWeight[3] = 0.0f;
        }
        material.sheenRoughness = sourceMaterial->sheen.sheen_roughness_factor;
    }

    float emissiveScale = sourceMaterial->has_emissive_strength
        ? sourceMaterial->emissive_strength.emissive_strength
        : 1.0f;
    vec3 emissive = {
        sourceMaterial->emissive_factor[0] * emissiveScale,
        sourceMaterial->emissive_factor[1] * emissiveScale,
        sourceMaterial->emissive_factor[2] * emissiveScale,
    };
    float emissiveMax = max3(emissive[0], emissive[1], emissive[2]);
    if (emissiveMax > 0.0f) {
        material.emissionColor[0] = emissive[0] / emissiveMax;
        material.emissionColor[1] = emissive[1] / emissiveMax;
        material.emissionColor[2] = emissive[2] / emissiveMax;
        material.emissionLuminance = emissiveMax;
    } else {
        material.emissionColor[0] = 1.0f;
        material.emissionColor[1] = 1.0f;
        material.emissionColor[2] = 1.0f;
        material.emissionLuminance = 0.0f;
    }

    uint32_t normalTextureIndex = VKRT_INVALID_INDEX;
    if (registerImportedTexture(
        importData,
        resolvedPath,
        &sourceMaterial->normal_texture,
        VKRT_TEXTURE_COLOR_SPACE_LINEAR,
        cachedImages,
        cachedColorSpaces,
        cachedTextureIndices,
        inoutCachedTextureCount,
        &normalTextureIndex
    )) {
        material.normalTextureIndex = normalTextureIndex;
        material.normalTextureScale = sourceMaterial->normal_texture.scale > 0.0f
            ? sourceMaterial->normal_texture.scale
            : 1.0f;
        material.normalTextureWrap = packTextureWrapModes(
            sourceMaterial->normal_texture.texture->sampler
                ? sourceMaterial->normal_texture.texture->sampler->wrap_s
                : cgltf_wrap_mode_repeat,
            sourceMaterial->normal_texture.texture->sampler
                ? sourceMaterial->normal_texture.texture->sampler->wrap_t
                : cgltf_wrap_mode_repeat
        );
        setMaterialTextureTexcoordSet(
            &material,
            VKRT_MATERIAL_TEXTURE_SLOT_NORMAL,
            queryTextureViewTexcoordSet(&sourceMaterial->normal_texture)
        );
        copyTextureTransform(
            material.normalTextureTransform,
            &material.textureRotations[VKRT_MATERIAL_TEXTURE_SLOT_NORMAL],
            &sourceMaterial->normal_texture
        );
    }

    uint32_t emissiveTextureIndex = VKRT_INVALID_INDEX;
    if (registerImportedTexture(
        importData,
        resolvedPath,
        &sourceMaterial->emissive_texture,
        VKRT_TEXTURE_COLOR_SPACE_SRGB,
        cachedImages,
        cachedColorSpaces,
        cachedTextureIndices,
        inoutCachedTextureCount,
        &emissiveTextureIndex
    )) {
        material.emissiveTextureIndex = emissiveTextureIndex;
        material.emissiveTextureWrap = packTextureWrapModes(
            sourceMaterial->emissive_texture.texture->sampler
                ? sourceMaterial->emissive_texture.texture->sampler->wrap_s
                : cgltf_wrap_mode_repeat,
            sourceMaterial->emissive_texture.texture->sampler
                ? sourceMaterial->emissive_texture.texture->sampler->wrap_t
                : cgltf_wrap_mode_repeat
        );
        setMaterialTextureTexcoordSet(
            &material,
            VKRT_MATERIAL_TEXTURE_SLOT_EMISSIVE,
            queryTextureViewTexcoordSet(&sourceMaterial->emissive_texture)
        );
        copyTextureTransform(
            material.emissiveTextureTransform,
            &material.textureRotations[VKRT_MATERIAL_TEXTURE_SLOT_EMISSIVE],
            &sourceMaterial->emissive_texture
        );
    }

    if (sourceMaterial->alpha_mode == cgltf_alpha_mode_mask) {
        material.alphaMode = VKRT_MATERIAL_ALPHA_MODE_MASK;
        material.alphaCutoff = sourceMaterial->alpha_cutoff;
    } else if (sourceMaterial->alpha_mode == cgltf_alpha_mode_blend) {
        material.alphaMode = VKRT_MATERIAL_ALPHA_MODE_BLEND;
        material.alphaCutoff = kAlphaBlendMaskCutoff;
    }

    return material;
}

static int buildNodeLocalTransformMatrix(const cgltf_node* node, mat4 outTransform) {
    if (!node || !outTransform) return 0;

    cgltf_float rawLocalTransform[16] = {0};
    cgltf_node_transform_local(node, rawLocalTransform);
    memcpy(outTransform, rawLocalTransform, sizeof(mat4));
    mat4 basisAdjustedTransform = GLM_MAT4_IDENTITY_INIT;
    buildImportedMeshNodeTransformMatrix(outTransform, basisAdjustedTransform);
    glm_mat4_copy(basisAdjustedTransform, outTransform);
    return 1;
}

static int appendNodeEntry(
    MeshImportData* importData,
    const cgltf_node* node,
    uint32_t parentIndex,
    uint32_t* outNodeIndex
) {
    if (!importData || !node || !outNodeIndex) return -1;

    mat4 localTransform = GLM_MAT4_IDENTITY_INIT;
    if (!buildNodeLocalTransformMatrix(node, localTransform)) return -1;

    NodeImportEntry entry = {0};
    entry.parentIndex = parentIndex;
    entry.meshEntryCount = 0u;
    memcpy(entry.localTransform, localTransform, sizeof(entry.localTransform));
    if (node->name && node->name[0]) {
        entry.name = stringDuplicate(node->name);
        if (!entry.name) {
            return -1;
        }
    }

    decomposeImportedMeshTransform(localTransform, entry.position, entry.rotation, entry.scale);
    if (appendImportNode(importData, &entry, outNodeIndex) != 0) {
        releaseImportNode(&entry);
        return -1;
    }
    return 0;
}

static void generateNormals(Vertex* vertices, size_t vertexCount, const uint32_t* indices, size_t indexCount) {
    if (!vertices || !indices || vertexCount == 0 || indexCount < 3) return;

    for (size_t vertexIndex = 0; vertexIndex < vertexCount; vertexIndex++) {
        vertices[vertexIndex].normal[0] = 0.0f;
        vertices[vertexIndex].normal[1] = 0.0f;
        vertices[vertexIndex].normal[2] = 0.0f;
        vertices[vertexIndex].normal[3] = 0.0f;
    }

    for (size_t indexOffset = 0; indexOffset + 2 < indexCount; indexOffset += 3) {
        uint32_t triangle[3];
        if (!loadTriangleIndices(vertexCount, indices, indexOffset, triangle)) continue;

        vec3 p0, p1, p2;
        loadVertexPosition3(vertices, triangle[0], p0);
        loadVertexPosition3(vertices, triangle[1], p1);
        loadVertexPosition3(vertices, triangle[2], p2);

        vec3 edge01 = GLM_VEC3_ZERO_INIT;
        vec3 edge02 = GLM_VEC3_ZERO_INIT;
        vec3 faceNormal = GLM_VEC3_ZERO_INIT;
        glm_vec3_sub(p1, p0, edge01);
        glm_vec3_sub(p2, p0, edge02);
        glm_vec3_cross(edge01, edge02, faceNormal);
        if (glm_vec3_norm2(faceNormal) <= 1e-12f) continue;

        glm_vec3_add(vertices[triangle[0]].normal, faceNormal, vertices[triangle[0]].normal);
        glm_vec3_add(vertices[triangle[1]].normal, faceNormal, vertices[triangle[1]].normal);
        glm_vec3_add(vertices[triangle[2]].normal, faceNormal, vertices[triangle[2]].normal);
    }

    for (size_t vertexIndex = 0; vertexIndex < vertexCount; vertexIndex++) {
        vec3 normal;
        loadVertexNormal3(vertices, (uint32_t)vertexIndex, normal);
        if (glm_vec3_norm2(normal) > 1e-12f) {
            glm_vec3_normalize(normal);
        } else {
            normal[2] = 1.0f;
        }

        copyFloat3ToFloat4(vertices[vertexIndex].normal, normal, 0.0f);
    }
}

static void generateTangents(
    Vertex* vertices,
    size_t vertexCount,
    const uint32_t* indices,
    size_t indexCount,
    const float* texcoords
)
{
    if (!vertices || !indices || !texcoords || vertexCount == 0 || indexCount < 3) {
        return;
    }

    vec3* tangent1 = (vec3*)calloc(vertexCount, sizeof(vec3));
    vec3* tangent2 = (vec3*)calloc(vertexCount, sizeof(vec3));
    if (!tangent1 || !tangent2) {
        free(tangent1);
        free(tangent2);
        for (size_t vertexIndex = 0; vertexIndex < vertexCount; vertexIndex++) {
            buildFallbackTangent(vertices[vertexIndex].normal, vertices[vertexIndex].tangent);
        }
        return;
    }

    for (size_t indexOffset = 0; indexOffset + 2 < indexCount; indexOffset += 3) {
        uint32_t triangle[3];
        if (!loadTriangleIndices(vertexCount, indices, indexOffset, triangle)) continue;

        vec3 p0, p1, p2;
        loadVertexPosition3(vertices, triangle[0], p0);
        loadVertexPosition3(vertices, triangle[1], p1);
        loadVertexPosition3(vertices, triangle[2], p2);

        uint32_t i0 = triangle[0];
        uint32_t i1 = triangle[1];
        uint32_t i2 = triangle[2];
        vec2 uv0 = {texcoords[i0 * 2u + 0u], texcoords[i0 * 2u + 1u]};
        vec2 uv1 = {texcoords[i1 * 2u + 0u], texcoords[i1 * 2u + 1u]};
        vec2 uv2 = {texcoords[i2 * 2u + 0u], texcoords[i2 * 2u + 1u]};

        vec3 edge1 = GLM_VEC3_ZERO_INIT;
        vec3 edge2 = GLM_VEC3_ZERO_INIT;
        glm_vec3_sub(p1, p0, edge1);
        glm_vec3_sub(p2, p0, edge2);

        float du1 = uv1[0] - uv0[0];
        float dv1 = uv1[1] - uv0[1];
        float du2 = uv2[0] - uv0[0];
        float dv2 = uv2[1] - uv0[1];
        float det = du1 * dv2 - dv1 * du2;
        if (fabsf(det) <= 1e-12f) continue;

        float invDet = 1.0f / det;
        vec3 sdir = {
            (dv2 * edge1[0] - dv1 * edge2[0]) * invDet,
            (dv2 * edge1[1] - dv1 * edge2[1]) * invDet,
            (dv2 * edge1[2] - dv1 * edge2[2]) * invDet,
        };
        vec3 tdir = {
            (du1 * edge2[0] - du2 * edge1[0]) * invDet,
            (du1 * edge2[1] - du2 * edge1[1]) * invDet,
            (du1 * edge2[2] - du2 * edge1[2]) * invDet,
        };

        glm_vec3_add(tangent1[i0], sdir, tangent1[i0]);
        glm_vec3_add(tangent1[i1], sdir, tangent1[i1]);
        glm_vec3_add(tangent1[i2], sdir, tangent1[i2]);
        glm_vec3_add(tangent2[i0], tdir, tangent2[i0]);
        glm_vec3_add(tangent2[i1], tdir, tangent2[i1]);
        glm_vec3_add(tangent2[i2], tdir, tangent2[i2]);
    }

    for (size_t vertexIndex = 0; vertexIndex < vertexCount; vertexIndex++) {
        vec3 tangent;
        glm_vec3_copy(tangent1[vertexIndex], tangent);
        vec3 normal;
        loadVertexNormal3(vertices, (uint32_t)vertexIndex, normal);
        if (glm_vec3_norm2(normal) <= 1e-12f || glm_vec3_norm2(tangent) <= 1e-12f) {
            buildFallbackTangent(vertices[vertexIndex].normal, vertices[vertexIndex].tangent);
            continue;
        }

        glm_vec3_normalize(normal);
        vec3 crossValue = GLM_VEC3_ZERO_INIT;
        glm_vec3_cross(normal, tangent, crossValue);
        float handedness = glm_vec3_dot(crossValue, tangent2[vertexIndex]) < 0.0f ? -1.0f : 1.0f;
        if (!orthonormalizeTangentFrame(
            vertices[vertexIndex].normal,
            tangent,
            handedness,
            vertices[vertexIndex].tangent
        )) {
            buildFallbackTangent(vertices[vertexIndex].normal, vertices[vertexIndex].tangent);
        }
    }

    free(tangent1);
    free(tangent2);
}

static void alignPrimitiveWindingToNormals(Vertex* vertices, size_t vertexCount, uint32_t* indices, size_t indexCount) {
    if (!vertices || !indices || vertexCount == 0 || indexCount < 3) {
        return;
    }

    for (size_t indexOffset = 0; indexOffset + 2 < indexCount; indexOffset += 3) {
        if (triangleNormalAlignment(vertices, vertexCount, indices, indexOffset) >= 0.0f) {
            continue;
        }
        uint32_t tmp = indices[indexOffset + 1];
        indices[indexOffset + 1] = indices[indexOffset + 2];
        indices[indexOffset + 2] = tmp;
    }
}

static int buildPrimitiveEntry(
    const cgltf_data* data,
    const cgltf_node* node,
    const cgltf_mesh* mesh,
    cgltf_size primitiveIndex,
    MeshImportEntry* outEntry
) {
    if (!data || !node || !mesh || !outEntry || primitiveIndex >= mesh->primitives_count) return -1;

    const cgltf_primitive* primitive = &mesh->primitives[primitiveIndex];
    if (primitive->type != cgltf_primitive_type_triangles) return 0;

    const cgltf_accessor* positionAccessor = findAttributeAccessor(primitive, cgltf_attribute_type_position, 0u);
    const cgltf_accessor* normalAccessor = findAttributeAccessor(primitive, cgltf_attribute_type_normal, 0u);
    const cgltf_accessor* tangentAccessor = findAttributeAccessor(primitive, cgltf_attribute_type_tangent, 0u);
    const cgltf_accessor* texcoordAccessor0 = findAttributeAccessor(primitive, cgltf_attribute_type_texcoord, 0u);
    const cgltf_accessor* texcoordAccessor1 = findAttributeAccessor(primitive, cgltf_attribute_type_texcoord, 1u);
    const cgltf_accessor* colorAccessor0 = findAttributeAccessor(primitive, cgltf_attribute_type_color, 0u);
    int useImportedTangents = normalAccessor && tangentAccessor;
    if (!positionAccessor || positionAccessor->count == 0) return 0;

    MeshImportEntry entry = {0};
    entry.vertexCount = (size_t)positionAccessor->count;
    entry.indexCount = primitive->indices
        ? (size_t)primitive->indices->count
        : (size_t)positionAccessor->count;
    entry.materialIndex = primitive->material ? (uint32_t)(primitive->material - data->materials) : VKRT_INVALID_INDEX;
    entry.renderBackfaces = primitive->material && primitive->material->double_sided ? 1u : 0u;
    const cgltf_material* sourceMaterial = primitive->material;
    uint32_t tangentTexcoordSet = 0u;
    if (sourceMaterial) {
        tangentTexcoordSet = queryTextureViewTexcoordSet(&sourceMaterial->normal_texture);
        if (tangentTexcoordSet > 1u) tangentTexcoordSet = 0u;
    }
    const cgltf_accessor* tangentTexcoordAccessor = tangentTexcoordSet == 1u ? texcoordAccessor1 : texcoordAccessor0;
    if (!validatePrimitiveAllocationFootprint(entry.vertexCount, entry.indexCount, !useImportedTangents && tangentTexcoordAccessor)) {
        return -1;
    }

    char generatedName[kGeneratedMeshNameCapacity];
    buildEntryName(generatedName, sizeof(generatedName), node, mesh, primitiveIndex);
    entry.name = stringDuplicate(generatedName);
    entry.vertices = (Vertex*)calloc(entry.vertexCount, sizeof(Vertex));
    entry.indices = (uint32_t*)malloc(entry.indexCount * sizeof(uint32_t));
    if (!entry.name || !entry.vertices || !entry.indices) {
        releaseImportEntry(&entry);
        return -1;
    }

    float* texcoords = NULL;
    if (!useImportedTangents && tangentTexcoordAccessor) {
        texcoords = (float*)calloc(entry.vertexCount * 2u, sizeof(float));
        if (!texcoords) {
            releaseImportEntry(&entry);
            return -1;
        }
    }

    for (size_t vertexIndex = 0; vertexIndex < entry.vertexCount; vertexIndex++) {
        float position[3] = {0.0f, 0.0f, 0.0f};
        float normal[3] = {0.0f, 0.0f, 1.0f};
        float tangent[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float texcoord0[2] = {0.0f, 0.0f};
        float texcoord1[2] = {0.0f, 0.0f};

        if (!cgltf_accessor_read_float(positionAccessor, (cgltf_size)vertexIndex, position, 3)) {
            free(texcoords);
            releaseImportEntry(&entry);
            return -1;
        }

        float enginePosition[3] = {position[0], -position[2], position[1]};
        copyFloat3ToFloat4(entry.vertices[vertexIndex].position, enginePosition, 1.0f);

        if (normalAccessor && cgltf_accessor_read_float(normalAccessor, (cgltf_size)vertexIndex, normal, 3)) {
            float engineNormal[3] = {normal[0], -normal[2], normal[1]};
            copyFloat3ToFloat4(entry.vertices[vertexIndex].normal, engineNormal, 0.0f);
        }

        if (useImportedTangents && cgltf_accessor_read_float(tangentAccessor, (cgltf_size)vertexIndex, tangent, 4)) {
            entry.vertices[vertexIndex].tangent[0] = tangent[0];
            entry.vertices[vertexIndex].tangent[1] = -tangent[2];
            entry.vertices[vertexIndex].tangent[2] = tangent[1];
            entry.vertices[vertexIndex].tangent[3] = tangent[3];
        }

        if (colorAccessor0) {
            cgltf_size colorComponentCount = cgltf_num_components(colorAccessor0->type);
            if (colorComponentCount < 3u || colorComponentCount > 4u ||
                !cgltf_accessor_read_float(colorAccessor0, (cgltf_size)vertexIndex, color, colorComponentCount)) {
                free(texcoords);
                releaseImportEntry(&entry);
                return -1;
            }
        }

        entry.vertices[vertexIndex].color[0] = color[0];
        entry.vertices[vertexIndex].color[1] = color[1];
        entry.vertices[vertexIndex].color[2] = color[2];
        entry.vertices[vertexIndex].color[3] = color[3];

        if (texcoordAccessor0) {
            if (!cgltf_accessor_read_float(texcoordAccessor0, (cgltf_size)vertexIndex, texcoord0, 2)) {
                free(texcoords);
                releaseImportEntry(&entry);
                return -1;
            }
        }

        entry.vertices[vertexIndex].texcoord0[0] = texcoord0[0];
        entry.vertices[vertexIndex].texcoord0[1] = texcoord0[1];

        if (texcoordAccessor1) {
            if (!cgltf_accessor_read_float(texcoordAccessor1, (cgltf_size)vertexIndex, texcoord1, 2)) {
                free(texcoords);
                releaseImportEntry(&entry);
                return -1;
            }
        }


        entry.vertices[vertexIndex].texcoord1[0] = texcoord1[0];
        entry.vertices[vertexIndex].texcoord1[1] = texcoord1[1];

        if (texcoords) {
            const float* tangentTexcoords = tangentTexcoordSet == 1u ? texcoord1 : texcoord0;
            texcoords[vertexIndex * 2u + 0u] = tangentTexcoords[0];
            texcoords[vertexIndex * 2u + 1u] = tangentTexcoords[1];
        }
    }

    if (primitive->indices) {
        for (size_t indexOffset = 0; indexOffset < entry.indexCount; indexOffset++) {
            cgltf_size indexValue = cgltf_accessor_read_index(primitive->indices, (cgltf_size)indexOffset);
            if (indexValue >= entry.vertexCount) {
                free(texcoords);
                releaseImportEntry(&entry);
                return -1;
            }
            entry.indices[indexOffset] = (uint32_t)indexValue;
        }
    } else {
        for (size_t indexOffset = 0; indexOffset < entry.indexCount; indexOffset++) {
            entry.indices[indexOffset] = (uint32_t)indexOffset;
        }
    }

    if (!normalAccessor) {
        generateNormals(entry.vertices, entry.vertexCount, entry.indices, entry.indexCount);
    }

    if (normalAccessor) {
        alignPrimitiveWindingToNormals(entry.vertices, entry.vertexCount, entry.indices, entry.indexCount);
    }

    if (useImportedTangents) {
        finalizeTangents(entry.vertices, entry.vertexCount);
    } else if (texcoords) {
        generateTangents(entry.vertices, entry.vertexCount, entry.indices, entry.indexCount, texcoords);
    } else {
        for (size_t vertexIndex = 0; vertexIndex < entry.vertexCount; vertexIndex++) {
            buildFallbackTangent(entry.vertices[vertexIndex].normal, entry.vertices[vertexIndex].tangent);
        }
    }

    free(texcoords);

    entry.nodeIndex = VKRT_INVALID_INDEX;
    glm_vec3_zero(entry.position);
    glm_vec3_zero(entry.rotation);
    glm_vec3_one(entry.scale);

    *outEntry = entry;
    return 1;
}

static int collectNodeEntries(const cgltf_data* data, const cgltf_node* node, uint32_t parentNodeIndex, MeshImportData* importData) {
    if (!data || !node || !importData) return -1;

    uint32_t nodeIndex = VKRT_INVALID_INDEX;
    if (appendNodeEntry(importData, node, parentNodeIndex, &nodeIndex) != 0) {
        return -1;
    }

    if (node->mesh) {
        for (cgltf_size primitiveIndex = 0; primitiveIndex < node->mesh->primitives_count; primitiveIndex++) {
            MeshImportEntry entry = {0};
            int buildResult = buildPrimitiveEntry(data, node, node->mesh, primitiveIndex, &entry);
            if (buildResult < 0) {
                releaseImportEntry(&entry);
                return -1;
            }
            if (buildResult == 0) continue;
            entry.nodeIndex = nodeIndex;
            if (appendImportEntry(importData, &entry) != 0) {
                releaseImportEntry(&entry);
                return -1;
            }
            importData->nodes[nodeIndex].meshEntryCount++;
        }
    }

    for (cgltf_size childIndex = 0; childIndex < node->children_count; childIndex++) {
        if (collectNodeEntries(data, node->children[childIndex], nodeIndex, importData) != 0) {
            return -1;
        }
    }

    return 0;
}

int meshLoadFromFile(const char* filePath, MeshImportData* outImportData) {
    if (!filePath || !filePath[0] || !outImportData) return -1;

    uint64_t totalStartTime = getMicroseconds();

    char resolvedPath[VKRT_PATH_MAX];
    if (resolveExistingPath(filePath, resolvedPath, sizeof(resolvedPath)) != 0) {
        LOG_ERROR("Mesh file not found: %s", filePath);
        return -1;
    }

    *outImportData = (MeshImportData){0};

    cgltf_options options = {0};
    cgltf_data* data = NULL;

    uint64_t parseStartTime = getMicroseconds();
    if (cgltf_parse_file(&options, resolvedPath, &data) != cgltf_result_success) {
        LOG_ERROR("Failed to parse GLTF '%s'", resolvedPath);
        return -1;
    }

    if (cgltf_load_buffers(&options, data, resolvedPath) != cgltf_result_success) {
        cgltf_free(data);
        LOG_ERROR("Failed to load buffers for '%s'", resolvedPath);
        return -1;
    }

    LOG_TRACE(
        "glTF parsed. File: %s, Nodes: %zu, Meshes: %zu, Materials: %zu, in %.3f ms",
        resolvedPath,
        data->nodes_count,
        data->meshes_count,
        data->materials_count,
        (double)(getMicroseconds() - parseStartTime) / 1e3
    );

    logIgnoredImportFeatures(resolvedPath, data);

    uint64_t materialStartTime = getMicroseconds();
    if (populateImportMaterials(data, outImportData, resolvedPath) != 0) {
        cgltf_free(data);
        meshReleaseImportData(outImportData);
        LOG_ERROR("Failed to extract material entries from '%s'", resolvedPath);
        return -1;
    }

    LOG_TRACE(
        "glTF materials extracted. File: %s, Materials: %u, Textures: %u, in %.3f ms",
        resolvedPath,
        outImportData->materialCount,
        outImportData->textureCount,
        (double)(getMicroseconds() - materialStartTime) / 1e3
    );

    uint64_t geometryStartTime = getMicroseconds();
    int result = 0;
    if (data->scene && data->scene->nodes_count > 0) {
        for (cgltf_size nodeIndex = 0; nodeIndex < data->scene->nodes_count; nodeIndex++) {
            if (collectNodeEntries(data, data->scene->nodes[nodeIndex], VKRT_INVALID_INDEX, outImportData) != 0) {
                result = -1;
                break;
            }
        }
    } else {
        for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; nodeIndex++) {
            if (data->nodes[nodeIndex].parent) continue;
            if (collectNodeEntries(data, &data->nodes[nodeIndex], VKRT_INVALID_INDEX, outImportData) != 0) {
                result = -1;
                break;
            }
        }
    }

    cgltf_free(data);

    if (result != 0) {
        meshReleaseImportData(outImportData);
        LOG_ERROR("Failed to extract mesh entries from '%s'", resolvedPath);
        return -1;
    }

    if (outImportData->count == 0) {
        meshReleaseImportData(outImportData);
        LOG_ERROR("No triangle mesh primitives were found in '%s'", resolvedPath);
        return -1;
    }

    LOG_TRACE(
        "glTF geometry extracted. File: %s, Meshes: %u, in %.3f ms",
        resolvedPath,
        outImportData->count,
        (double)(getMicroseconds() - geometryStartTime) / 1e3
    );
    LOG_TRACE(
        "glTF import decode complete. File: %s, Meshes: %u, Materials: %u, Textures: %u, cumulative: %.3f ms",
        resolvedPath,
        outImportData->count,
        outImportData->materialCount,
        outImportData->textureCount,
        (double)(getMicroseconds() - totalStartTime) / 1e3
    );

    return 0;
}
