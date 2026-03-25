#include "controller.h"

#include "mesh/controller.h"
#include "debug.h"
#include "io.h"

#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    kSceneFileVersion = 1,
    kPathComponentCapacity = 128,
};

static const char* kDefaultScenePath = "assets/scenes/cornell.json";

typedef struct LoadedMeshImportBatch {
    uint32_t firstMeshIndex;
    uint32_t meshCount;
} LoadedMeshImportBatch;

static FILE* openFileForWrite(const char* path) {
    if (!path || !path[0]) return NULL;
#if defined(_WIN32)
    FILE* file = NULL;
    return fopen_s(&file, path, "wb") == 0 ? file : NULL;
#else
    return fopen(path, "wb");
#endif
}

static int readTextFile(const char* path, char** outText) {
    if (outText) *outText = NULL;
    if (!path || !path[0] || !outText) return 0;

#if defined(_WIN32)
    FILE* file = NULL;
    if (fopen_s(&file, path, "rb") != 0) return 0;
#else
    FILE* file = fopen(path, "rb");
    if (!file) return 0;
#endif

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    long fileSize = ftell(file);
    if (fileSize < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    char* text = (char*)malloc((size_t)fileSize + 1u);
    if (!text) {
        fclose(file);
        return 0;
    }

    size_t bytesRead = fread(text, 1, (size_t)fileSize, file);
    fclose(file);
    if (bytesRead != (size_t)fileSize) {
        free(text);
        return 0;
    }

    text[fileSize] = '\0';
    *outText = text;
    return 1;
}

static int copyParentDirectory(const char* path, char outDirectory[VKRT_PATH_MAX]) {
    if (!path || !path[0] || !outDirectory) return 0;
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

static int joinPath(char* outPath, size_t outPathSize, const char* base, const char* value) {
    if (!outPath || outPathSize == 0u || !base || !base[0] || !value || !value[0]) return 0;

    size_t baseLength = strlen(base);
    const char* separator = (base[baseLength - 1] == '/' || base[baseLength - 1] == '\\') ? "" : "/";
    int written = snprintf(outPath, outPathSize, "%s%s%s", base, separator, value);
    return written > 0 && (size_t)written < outPathSize;
}

static int pathIsAbsolute(const char* path) {
    if (!path || !path[0]) return 0;
#if defined(_WIN32)
    if ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) {
        return path[1] == ':';
    }
    return path[0] == '\\' || path[0] == '/';
#else
    return path[0] == '/';
#endif
}

static int resolveSceneAssetPath(const char* scenePath, const char* storedPath, char outResolvedPath[VKRT_PATH_MAX]) {
    if (!storedPath || !storedPath[0] || !outResolvedPath) return 0;

    if (pathIsAbsolute(storedPath)) {
        return resolveExistingPath(storedPath, outResolvedPath, VKRT_PATH_MAX) == 0;
    }

    if (scenePath && scenePath[0]) {
        char parentDirectory[VKRT_PATH_MAX];
        char candidate[VKRT_PATH_MAX];
        if (copyParentDirectory(scenePath, parentDirectory) &&
            joinPath(candidate, sizeof(candidate), parentDirectory, storedPath) &&
            resolveExistingPath(candidate, outResolvedPath, VKRT_PATH_MAX) == 0) {
            return 1;
        }
    }

    return resolveExistingPath(storedPath, outResolvedPath, VKRT_PATH_MAX) == 0;
}

static void normalizePathSeparators(char* path) {
    if (!path) return;
    for (char* cursor = path; *cursor; cursor++) {
        if (*cursor == '\\') *cursor = '/';
    }
}

static size_t queryPathRootLength(const char* path) {
    if (!path || !path[0]) return 0u;
#if defined(_WIN32)
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':') {
        return 2u;
    }
#endif
    return path[0] == '/' ? 1u : 0u;
}

static int pathCharsEqual(char left, char right) {
#if defined(_WIN32)
    return tolower((unsigned char)left) == tolower((unsigned char)right);
#else
    return left == right;
#endif
}

static int pathComponentsEqual(const char* left, const char* right) {
    if (!left || !right) return 0;

    while (*left && *right) {
        if (!pathCharsEqual(*left, *right)) return 0;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static uint32_t splitPathComponents(char* path, size_t rootLength, char** components, uint32_t componentCapacity) {
    if (!path || !components || componentCapacity == 0u) return UINT32_MAX;

    uint32_t count = 0u;
    size_t index = rootLength;
    while (path[index] == '/') index++;

    while (path[index]) {
        if (count >= componentCapacity) return UINT32_MAX;
        components[count++] = &path[index];
        while (path[index] && path[index] != '/') index++;
        if (!path[index]) break;
        path[index++] = '\0';
        while (path[index] == '/') index++;
    }

    return count;
}

static int appendPathSegment(char* outPath, size_t outPathSize, size_t* inOutLength, const char* segment) {
    if (!outPath || outPathSize == 0u || !inOutLength || !segment || !segment[0]) return 0;

    size_t length = *inOutLength;
    if (length > 0u) {
        if (length + 1u >= outPathSize) return 0;
        outPath[length++] = '/';
    }

    size_t segmentLength = strlen(segment);
    if (length + segmentLength >= outPathSize) return 0;

    memcpy(outPath + length, segment, segmentLength);
    length += segmentLength;
    outPath[length] = '\0';
    *inOutLength = length;
    return 1;
}

static int copyPortableStoredPath(const char* scenePath, const char* sourcePath, char outStoredPath[VKRT_PATH_MAX]) {
    if (!sourcePath || !sourcePath[0] || !outStoredPath) return 0;

    char resolvedSource[VKRT_PATH_MAX];
    const char* sourceForStorage = sourcePath;
    if (!pathIsAbsolute(sourcePath) && resolveExistingPath(sourcePath, resolvedSource, sizeof(resolvedSource)) == 0) {
        sourceForStorage = resolvedSource;
    }

    if (snprintf(outStoredPath, VKRT_PATH_MAX, "%s", sourceForStorage) >= VKRT_PATH_MAX) return 0;
    normalizePathSeparators(outStoredPath);

    if (!scenePath || !scenePath[0] || !pathIsAbsolute(outStoredPath)) {
        return 1;
    }

    char sceneDirectory[VKRT_PATH_MAX];
    char sceneNormalized[VKRT_PATH_MAX];
    char sourceNormalized[VKRT_PATH_MAX];
    if (!copyParentDirectory(scenePath, sceneDirectory) ||
        snprintf(sceneNormalized, sizeof(sceneNormalized), "%s", sceneDirectory) >= (int)sizeof(sceneNormalized) ||
        snprintf(sourceNormalized, sizeof(sourceNormalized), "%s", outStoredPath) >= (int)sizeof(sourceNormalized)) {
        return 1;
    }

    normalizePathSeparators(sceneNormalized);
    normalizePathSeparators(sourceNormalized);
    if (!pathIsAbsolute(sceneNormalized) || !pathIsAbsolute(sourceNormalized)) return 1;

    size_t sceneRootLength = queryPathRootLength(sceneNormalized);
    size_t sourceRootLength = queryPathRootLength(sourceNormalized);
    if (sceneRootLength != sourceRootLength) return 1;
    for (size_t i = 0; i < sceneRootLength; i++) {
        if (!pathCharsEqual(sceneNormalized[i], sourceNormalized[i])) return 1;
    }

    char* sceneComponents[kPathComponentCapacity];
    char* sourceComponents[kPathComponentCapacity];
    uint32_t sceneComponentCount = splitPathComponents(
        sceneNormalized,
        sceneRootLength,
        sceneComponents,
        VKRT_ARRAY_COUNT(sceneComponents)
    );
    uint32_t sourceComponentCount = splitPathComponents(
        sourceNormalized,
        sourceRootLength,
        sourceComponents,
        VKRT_ARRAY_COUNT(sourceComponents)
    );
    if (sceneComponentCount == UINT32_MAX || sourceComponentCount == UINT32_MAX) return 1;

    uint32_t commonComponentCount = 0u;
    while (commonComponentCount < sceneComponentCount &&
           commonComponentCount < sourceComponentCount &&
           pathComponentsEqual(sceneComponents[commonComponentCount], sourceComponents[commonComponentCount])) {
        commonComponentCount++;
    }

    char relativePath[VKRT_PATH_MAX];
    size_t relativeLength = 0u;
    relativePath[0] = '\0';
    for (uint32_t i = commonComponentCount; i < sceneComponentCount; i++) {
        if (!appendPathSegment(relativePath, sizeof(relativePath), &relativeLength, "..")) {
            return 1;
        }
    }
    for (uint32_t i = commonComponentCount; i < sourceComponentCount; i++) {
        if (!appendPathSegment(relativePath, sizeof(relativePath), &relativeLength, sourceComponents[i])) {
            return 1;
        }
    }

    if (relativeLength == 0u) {
        if (snprintf(outStoredPath, VKRT_PATH_MAX, ".") >= VKRT_PATH_MAX) return 0;
        return 1;
    }

    if (snprintf(outStoredPath, VKRT_PATH_MAX, "%s", relativePath) >= VKRT_PATH_MAX) return 0;
    return 1;
}

static cJSON* createFloatArray(const float* values, size_t count) {
    cJSON* array = cJSON_CreateArray();
    if (!array) return NULL;
    for (size_t i = 0; i < count; i++) {
        cJSON_AddItemToArray(array, cJSON_CreateNumber((double)values[i]));
    }
    return array;
}

static void addUIntOrNull(cJSON* object, const char* name, uint32_t value) {
    if (!object || !name) return;
    if (value == VKRT_INVALID_INDEX) {
        cJSON_AddNullToObject(object, name);
    } else {
        cJSON_AddNumberToObject(object, name, (double)value);
    }
}

static int jsonToUInt32(const cJSON* item, uint32_t* outValue) {
    if (outValue) *outValue = 0u;
    if (!item || !cJSON_IsNumber(item) || !outValue) return 0;
    double value = item->valuedouble;
    if (value < 0.0 || value > (double)UINT32_MAX) return 0;
    uint32_t converted = (uint32_t)value;
    if ((double)converted != value) return 0;
    *outValue = converted;
    return 1;
}

static int jsonToFloat(const cJSON* item, float* outValue) {
    if (outValue) *outValue = 0.0f;
    if (!item || !cJSON_IsNumber(item) || !outValue) return 0;
    *outValue = (float)item->valuedouble;
    return 1;
}

static int jsonToBool(const cJSON* item, uint8_t* outValue) {
    if (outValue) *outValue = 0u;
    if (!item || !cJSON_IsBool(item) || !outValue) return 0;
    *outValue = cJSON_IsTrue(item) ? 1u : 0u;
    return 1;
}

static int jsonToFloatArray(const cJSON* item, float* outValues, size_t count) {
    if (!item || !cJSON_IsArray(item) || !outValues) return 0;
    if ((size_t)cJSON_GetArraySize(item) != count) return 0;

    for (size_t i = 0; i < count; i++) {
        if (!jsonToFloat(cJSON_GetArrayItem((cJSON*)item, (int)i), &outValues[i])) {
            return 0;
        }
    }
    return 1;
}

static int jsonToMat4(const cJSON* item, mat4 outMatrix) {
    float values[16];
    if (!jsonToFloatArray(item, values, VKRT_ARRAY_COUNT(values))) return 0;

    size_t valueIndex = 0u;
    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            outMatrix[column][row] = values[valueIndex++];
        }
    }
    return 1;
}

static int jsonToIndexOrInvalid(const cJSON* item, uint32_t* outValue) {
    if (outValue) *outValue = VKRT_INVALID_INDEX;
    if (!outValue || !item) return 0;
    if (cJSON_IsNull(item)) return 1;
    return jsonToUInt32(item, outValue);
}

static int jsonReadOptionalUInt32Field(const cJSON* object, const char* name, uint32_t* outValue) {
    if (!object || !cJSON_IsObject(object) || !name || !outValue) return 0;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive((cJSON*)object, name);
    return item ? jsonToUInt32(item, outValue) : 1;
}

static int jsonReadOptionalFloatField(const cJSON* object, const char* name, float* outValue) {
    if (!object || !cJSON_IsObject(object) || !name || !outValue) return 0;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive((cJSON*)object, name);
    return item ? jsonToFloat(item, outValue) : 1;
}

static int jsonReadOptionalBoolField(const cJSON* object, const char* name, uint8_t* outValue) {
    if (!object || !cJSON_IsObject(object) || !name || !outValue) return 0;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive((cJSON*)object, name);
    return item ? jsonToBool(item, outValue) : 1;
}

static int jsonReadOptionalFloatArrayField(const cJSON* object, const char* name, float* outValues, size_t count) {
    if (!object || !cJSON_IsObject(object) || !name || !outValues) return 0;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive((cJSON*)object, name);
    return item ? jsonToFloatArray(item, outValues, count) : 1;
}

static int jsonReadOptionalIndexField(const cJSON* object, const char* name, uint32_t* outValue) {
    if (!object || !cJSON_IsObject(object) || !name || !outValue) return 0;
    const cJSON* item = cJSON_GetObjectItemCaseSensitive((cJSON*)object, name);
    return item ? jsonToIndexOrInvalid(item, outValue) : 1;
}

static cJSON* createMaterialJSON(const Material* material) {
    if (!material) return NULL;

    Material defaults = VKRT_materialDefault();
    cJSON* object = cJSON_CreateObject();
    if (!object) return NULL;

    if (memcmp(material->baseColor, defaults.baseColor, sizeof(material->baseColor)) != 0) {
        cJSON_AddItemToObject(object, "baseColor", createFloatArray(material->baseColor, 3u));
    }
    if (material->roughness != defaults.roughness) cJSON_AddNumberToObject(object, "roughness", material->roughness);
    if (memcmp(material->emissionColor, defaults.emissionColor, sizeof(material->emissionColor)) != 0) {
        cJSON_AddItemToObject(object, "emissionColor", createFloatArray(material->emissionColor, 3u));
    }
    if (material->emissionLuminance != defaults.emissionLuminance) cJSON_AddNumberToObject(object, "emissionLuminance", material->emissionLuminance);
    if (memcmp(material->eta, defaults.eta, sizeof(material->eta)) != 0) {
        cJSON_AddItemToObject(object, "eta", createFloatArray(material->eta, 3u));
    }
    if (material->metallic != defaults.metallic) cJSON_AddNumberToObject(object, "metallic", material->metallic);
    if (memcmp(material->k, defaults.k, sizeof(material->k)) != 0) {
        cJSON_AddItemToObject(object, "k", createFloatArray(material->k, 3u));
    }
    if (material->anisotropic != defaults.anisotropic) cJSON_AddNumberToObject(object, "anisotropic", material->anisotropic);
    if (material->specular != defaults.specular) cJSON_AddNumberToObject(object, "specular", material->specular);
    if (material->specularTint != defaults.specularTint) cJSON_AddNumberToObject(object, "specularTint", material->specularTint);
    if (memcmp(material->sheenTintWeight, defaults.sheenTintWeight, sizeof(material->sheenTintWeight)) != 0) {
        cJSON_AddItemToObject(object, "sheenTintWeight", createFloatArray(material->sheenTintWeight, 4u));
    }
    if (material->clearcoat != defaults.clearcoat) cJSON_AddNumberToObject(object, "clearcoat", material->clearcoat);
    if (material->clearcoatGloss != defaults.clearcoatGloss) cJSON_AddNumberToObject(object, "clearcoatGloss", material->clearcoatGloss);
    if (material->ior != defaults.ior) cJSON_AddNumberToObject(object, "ior", material->ior);
    if (material->diffuseRoughness != defaults.diffuseRoughness) cJSON_AddNumberToObject(object, "diffuseRoughness", material->diffuseRoughness);
    if (material->transmission != defaults.transmission) cJSON_AddNumberToObject(object, "transmission", material->transmission);
    if (material->subsurface != defaults.subsurface) cJSON_AddNumberToObject(object, "subsurface", material->subsurface);
    if (material->sheenRoughness != defaults.sheenRoughness) cJSON_AddNumberToObject(object, "sheenRoughness", material->sheenRoughness);
    if (material->absorptionCoefficient != defaults.absorptionCoefficient) cJSON_AddNumberToObject(object, "absorptionCoefficient", material->absorptionCoefficient);
    if (memcmp(material->attenuationColor, defaults.attenuationColor, sizeof(material->attenuationColor)) != 0) {
        cJSON_AddItemToObject(object, "attenuationColor", createFloatArray(material->attenuationColor, 3u));
    }
    if (material->normalTextureScale != defaults.normalTextureScale) cJSON_AddNumberToObject(object, "normalTextureScale", material->normalTextureScale);
    if (material->baseColorTextureIndex != defaults.baseColorTextureIndex) addUIntOrNull(object, "baseColorTextureIndex", material->baseColorTextureIndex);
    if (material->metallicRoughnessTextureIndex != defaults.metallicRoughnessTextureIndex) addUIntOrNull(object, "metallicRoughnessTextureIndex", material->metallicRoughnessTextureIndex);
    if (material->normalTextureIndex != defaults.normalTextureIndex) addUIntOrNull(object, "normalTextureIndex", material->normalTextureIndex);
    if (material->emissiveTextureIndex != defaults.emissiveTextureIndex) addUIntOrNull(object, "emissiveTextureIndex", material->emissiveTextureIndex);
    if (material->baseColorTextureWrap != defaults.baseColorTextureWrap) cJSON_AddNumberToObject(object, "baseColorTextureWrap", material->baseColorTextureWrap);
    if (material->metallicRoughnessTextureWrap != defaults.metallicRoughnessTextureWrap) cJSON_AddNumberToObject(object, "metallicRoughnessTextureWrap", material->metallicRoughnessTextureWrap);
    if (material->normalTextureWrap != defaults.normalTextureWrap) cJSON_AddNumberToObject(object, "normalTextureWrap", material->normalTextureWrap);
    if (material->emissiveTextureWrap != defaults.emissiveTextureWrap) cJSON_AddNumberToObject(object, "emissiveTextureWrap", material->emissiveTextureWrap);
    if (material->opacity != defaults.opacity) cJSON_AddNumberToObject(object, "opacity", material->opacity);
    if (material->alphaCutoff != defaults.alphaCutoff) cJSON_AddNumberToObject(object, "alphaCutoff", material->alphaCutoff);
    if (material->alphaMode != defaults.alphaMode) cJSON_AddNumberToObject(object, "alphaMode", material->alphaMode);
    if (material->textureTexcoordSets != defaults.textureTexcoordSets) cJSON_AddNumberToObject(object, "textureTexcoordSets", material->textureTexcoordSets);
    if (memcmp(material->baseColorTextureTransform, defaults.baseColorTextureTransform, sizeof(material->baseColorTextureTransform)) != 0) {
        cJSON_AddItemToObject(object, "baseColorTextureTransform", createFloatArray(material->baseColorTextureTransform, 4u));
    }
    if (memcmp(material->metallicRoughnessTextureTransform, defaults.metallicRoughnessTextureTransform, sizeof(material->metallicRoughnessTextureTransform)) != 0) {
        cJSON_AddItemToObject(object, "metallicRoughnessTextureTransform", createFloatArray(material->metallicRoughnessTextureTransform, 4u));
    }
    if (memcmp(material->normalTextureTransform, defaults.normalTextureTransform, sizeof(material->normalTextureTransform)) != 0) {
        cJSON_AddItemToObject(object, "normalTextureTransform", createFloatArray(material->normalTextureTransform, 4u));
    }
    if (memcmp(material->emissiveTextureTransform, defaults.emissiveTextureTransform, sizeof(material->emissiveTextureTransform)) != 0) {
        cJSON_AddItemToObject(object, "emissiveTextureTransform", createFloatArray(material->emissiveTextureTransform, 4u));
    }
    if (memcmp(material->textureRotations, defaults.textureRotations, sizeof(material->textureRotations)) != 0) {
        cJSON_AddItemToObject(object, "textureRotations", createFloatArray(material->textureRotations, 4u));
    }
    return object;
}

static int parseMaterialJSON(const cJSON* object, Material* outMaterial) {
    if (!object || !cJSON_IsObject(object) || !outMaterial) return 0;

    Material material = VKRT_materialDefault();
    if (!jsonReadOptionalFloatArrayField(object, "baseColor", material.baseColor, 3u) ||
        !jsonReadOptionalFloatField(object, "roughness", &material.roughness) ||
        !jsonReadOptionalFloatArrayField(object, "emissionColor", material.emissionColor, 3u) ||
        !jsonReadOptionalFloatField(object, "emissionLuminance", &material.emissionLuminance) ||
        !jsonReadOptionalFloatArrayField(object, "eta", material.eta, 3u) ||
        !jsonReadOptionalFloatField(object, "metallic", &material.metallic) ||
        !jsonReadOptionalFloatArrayField(object, "k", material.k, 3u) ||
        !jsonReadOptionalFloatField(object, "anisotropic", &material.anisotropic) ||
        !jsonReadOptionalFloatField(object, "specular", &material.specular) ||
        !jsonReadOptionalFloatField(object, "specularTint", &material.specularTint) ||
        !jsonReadOptionalFloatArrayField(object, "sheenTintWeight", material.sheenTintWeight, 4u) ||
        !jsonReadOptionalFloatField(object, "clearcoat", &material.clearcoat) ||
        !jsonReadOptionalFloatField(object, "clearcoatGloss", &material.clearcoatGloss) ||
        !jsonReadOptionalFloatField(object, "ior", &material.ior) ||
        !jsonReadOptionalFloatField(object, "diffuseRoughness", &material.diffuseRoughness) ||
        !jsonReadOptionalFloatField(object, "transmission", &material.transmission) ||
        !jsonReadOptionalFloatField(object, "subsurface", &material.subsurface) ||
        !jsonReadOptionalFloatField(object, "sheenRoughness", &material.sheenRoughness) ||
        !jsonReadOptionalFloatField(object, "absorptionCoefficient", &material.absorptionCoefficient) ||
        !jsonReadOptionalFloatArrayField(object, "attenuationColor", material.attenuationColor, 3u) ||
        !jsonReadOptionalFloatField(object, "normalTextureScale", &material.normalTextureScale) ||
        !jsonReadOptionalIndexField(object, "baseColorTextureIndex", &material.baseColorTextureIndex) ||
        !jsonReadOptionalIndexField(object, "metallicRoughnessTextureIndex", &material.metallicRoughnessTextureIndex) ||
        !jsonReadOptionalIndexField(object, "normalTextureIndex", &material.normalTextureIndex) ||
        !jsonReadOptionalIndexField(object, "emissiveTextureIndex", &material.emissiveTextureIndex) ||
        !jsonReadOptionalUInt32Field(object, "baseColorTextureWrap", &material.baseColorTextureWrap) ||
        !jsonReadOptionalUInt32Field(object, "metallicRoughnessTextureWrap", &material.metallicRoughnessTextureWrap) ||
        !jsonReadOptionalUInt32Field(object, "normalTextureWrap", &material.normalTextureWrap) ||
        !jsonReadOptionalUInt32Field(object, "emissiveTextureWrap", &material.emissiveTextureWrap) ||
        !jsonReadOptionalFloatField(object, "opacity", &material.opacity) ||
        !jsonReadOptionalFloatField(object, "alphaCutoff", &material.alphaCutoff) ||
        !jsonReadOptionalUInt32Field(object, "alphaMode", &material.alphaMode) ||
        !jsonReadOptionalUInt32Field(object, "textureTexcoordSets", &material.textureTexcoordSets) ||
        !jsonReadOptionalFloatArrayField(object, "baseColorTextureTransform", material.baseColorTextureTransform, 4u) ||
        !jsonReadOptionalFloatArrayField(object, "metallicRoughnessTextureTransform", material.metallicRoughnessTextureTransform, 4u) ||
        !jsonReadOptionalFloatArrayField(object, "normalTextureTransform", material.normalTextureTransform, 4u) ||
        !jsonReadOptionalFloatArrayField(object, "emissiveTextureTransform", material.emissiveTextureTransform, 4u) ||
        !jsonReadOptionalFloatArrayField(object, "textureRotations", material.textureRotations, 4u)) {
        return 0;
    }

    *outMaterial = material;
    return 1;
}

static void remapStandaloneTextureIndices(Material* material, const uint32_t* textureIndexMap, uint32_t textureIndexMapCount) {
    if (!material || !textureIndexMap) return;

    uint32_t* textureIndices[] = {
        &material->baseColorTextureIndex,
        &material->metallicRoughnessTextureIndex,
        &material->normalTextureIndex,
        &material->emissiveTextureIndex,
    };

    for (uint32_t i = 0; i < VKRT_ARRAY_COUNT(textureIndices); i++) {
        if (*textureIndices[i] == VKRT_INVALID_INDEX || *textureIndices[i] >= textureIndexMapCount) continue;
        if (textureIndexMap[*textureIndices[i]] != VKRT_INVALID_INDEX) {
            *textureIndices[i] = textureIndexMap[*textureIndices[i]];
        }
    }
}

static int clearCurrentScene(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return 0;

    uint32_t meshCount = 0u;
    if (VKRT_getMeshCount(vkrt, &meshCount) != VKRT_SUCCESS) return 0;
    for (uint32_t i = meshCount; i > 0u; i--) {
        if (VKRT_removeMesh(vkrt, i - 1u) != VKRT_SUCCESS) return 0;
    }

    uint32_t materialCount = 0u;
    if (VKRT_getMaterialCount(vkrt, &materialCount) != VKRT_SUCCESS) return 0;
    for (uint32_t i = materialCount; i > 1u; i--) {
        if (VKRT_removeMaterial(vkrt, i - 1u) != VKRT_SUCCESS) return 0;
    }

    uint32_t textureCount = 0u;
    if (VKRT_getTextureCount(vkrt, &textureCount) != VKRT_SUCCESS) return 0;
    for (uint32_t i = textureCount; i > 0u; i--) {
        if (VKRT_removeTexture(vkrt, i - 1u) != VKRT_SUCCESS) return 0;
    }

    if (VKRT_clearEnvironmentTexture(vkrt) != VKRT_SUCCESS) return 0;
    if (VKRT_setSceneTimeline(vkrt, NULL) != VKRT_SUCCESS) return 0;

    sessionResetSceneState(session);
    return 1;
}

static int querySavedTextureIndexMapCount(const cJSON* textureImportsArray, uint32_t* outCount) {
    if (outCount) *outCount = 0u;
    if (!outCount) return 0;
    if (textureImportsArray && !cJSON_IsArray(textureImportsArray)) return 0;

    uint32_t maxSavedTextureIndex = 0u;
    int haveSavedTextureIndex = 0;
    cJSON* textureObject = NULL;
    cJSON_ArrayForEach(textureObject, (cJSON*)textureImportsArray) {
        uint32_t textureIndex = 0u;
        uint32_t colorSpace = 0u;
        if (!cJSON_IsObject(textureObject) ||
            !jsonToUInt32(cJSON_GetObjectItemCaseSensitive(textureObject, "index"), &textureIndex) ||
            !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(textureObject, "path")) ||
            !jsonToUInt32(cJSON_GetObjectItemCaseSensitive(textureObject, "colorSpace"), &colorSpace)) {
            return 0;
        }

        if (!haveSavedTextureIndex || textureIndex > maxSavedTextureIndex) {
            maxSavedTextureIndex = textureIndex;
            haveSavedTextureIndex = 1;
        }
    }

    *outCount = haveSavedTextureIndex ? maxSavedTextureIndex + 1u : 0u;
    return 1;
}

static int addSceneObjectTransformJSON(cJSON* objectJSON, const SessionSceneObject* object) {
    if (!objectJSON || !object) return 0;

    cJSON* position = createFloatArray(object->localPosition, 3u);
    cJSON* rotation = createFloatArray(object->localRotation, 3u);
    cJSON* scale = createFloatArray(object->localScale, 3u);
    if (!position || !rotation || !scale) {
        cJSON_Delete(position);
        cJSON_Delete(rotation);
        cJSON_Delete(scale);
        return 0;
    }

    cJSON_AddItemToObject(objectJSON, "localPosition", position);
    cJSON_AddItemToObject(objectJSON, "localRotation", rotation);
    cJSON_AddItemToObject(objectJSON, "localScale", scale);
    return 1;
}

static int parseSceneObjectTransformJSON(
    const cJSON* sceneObject,
    vec3 outPosition,
    vec3 outRotation,
    vec3 outScale,
    mat4 outMatrix,
    uint8_t* outUseMatrix
) {
    if (outUseMatrix) *outUseMatrix = 0u;
    if (!sceneObject || !cJSON_IsObject(sceneObject) ||
        !outPosition || !outRotation || !outScale || !outMatrix || !outUseMatrix) {
        return 0;
    }

    const cJSON* positionObject = cJSON_GetObjectItemCaseSensitive((cJSON*)sceneObject, "localPosition");
    const cJSON* rotationObject = cJSON_GetObjectItemCaseSensitive((cJSON*)sceneObject, "localRotation");
    const cJSON* scaleObject = cJSON_GetObjectItemCaseSensitive((cJSON*)sceneObject, "localScale");
    if (positionObject || rotationObject || scaleObject) {
        if (!positionObject || !rotationObject || !scaleObject ||
            !jsonToFloatArray(positionObject, outPosition, 3u) ||
            !jsonToFloatArray(rotationObject, outRotation, 3u) ||
            !jsonToFloatArray(scaleObject, outScale, 3u)) {
            return 0;
        }
        *outUseMatrix = 0u;
        return 1;
    }

    if (!jsonToMat4(cJSON_GetObjectItemCaseSensitive((cJSON*)sceneObject, "localTransform"), outMatrix)) {
        return 0;
    }

    *outUseMatrix = 1u;
    return 1;
}

static int saveSceneSettings(cJSON* root, const VKRT_SceneSettingsSnapshot* settings) {
    if (!root || !settings) return 0;

    cJSON* object = cJSON_CreateObject();
    if (!object) return 0;

    cJSON* camera = cJSON_CreateObject();
    if (!camera) {
        cJSON_Delete(object);
        return 0;
    }
    cJSON_AddItemToObject(camera, "position", createFloatArray(settings->camera.pos, 3u));
    cJSON_AddItemToObject(camera, "target", createFloatArray(settings->camera.target, 3u));
    cJSON_AddItemToObject(camera, "up", createFloatArray(settings->camera.up, 3u));
    cJSON_AddNumberToObject(camera, "vfov", settings->camera.vfov);
    cJSON_AddItemToObject(object, "camera", camera);
    cJSON_AddNumberToObject(object, "samplesPerPixel", settings->samplesPerPixel);
    cJSON_AddNumberToObject(object, "rrMinDepth", settings->rrMinDepth);
    cJSON_AddNumberToObject(object, "rrMaxDepth", settings->rrMaxDepth);
    cJSON_AddNumberToObject(object, "toneMappingMode", settings->toneMappingMode);
    cJSON_AddNumberToObject(object, "exposure", settings->exposure);
    cJSON_AddBoolToObject(object, "autoExposureEnabled", settings->autoExposureEnabled != 0u);
    cJSON_AddBoolToObject(object, "autoSPPEnabled", settings->autoSPPEnabled != 0u);
    cJSON_AddItemToObject(object, "environmentColor", createFloatArray(settings->environmentColor, 3u));
    cJSON_AddNumberToObject(object, "environmentStrength", settings->environmentStrength);
    cJSON_AddNumberToObject(object, "debugMode", settings->debugMode);
    cJSON_AddBoolToObject(object, "misNeeEnabled", settings->misNeeEnabled != 0u);
    addUIntOrNull(object, "selectedMeshIndex", settings->selectedMeshIndex);

    cJSON_AddItemToObject(root, "sceneSettings", object);
    return 1;
}

static int applySceneSettings(
    VKRT* vkrt,
    const cJSON* settingsObject,
    const uint32_t* savedToLoadedMeshIndex,
    uint32_t savedMeshCount
) {
    if (!vkrt || !settingsObject || !cJSON_IsObject(settingsObject)) return 0;

    const cJSON* cameraObject = cJSON_GetObjectItemCaseSensitive((cJSON*)settingsObject, "camera");
    if (!cameraObject || !cJSON_IsObject(cameraObject)) {
        return 0;
    }

    VKRT_SceneSettingsSnapshot settings = {0};
    if (VKRT_getSceneSettings(vkrt, &settings) != VKRT_SUCCESS) {
        return 0;
    }

    vec3 cameraPosition = {settings.camera.pos[0], settings.camera.pos[1], settings.camera.pos[2]};
    vec3 cameraTarget = {settings.camera.target[0], settings.camera.target[1], settings.camera.target[2]};
    vec3 cameraUp = {settings.camera.up[0], settings.camera.up[1], settings.camera.up[2]};
    float vfov = settings.camera.vfov;
    uint32_t samplesPerPixel = settings.samplesPerPixel;
    uint32_t rrMinDepth = settings.rrMinDepth;
    uint32_t rrMaxDepth = settings.rrMaxDepth;
    uint32_t toneMappingMode = settings.toneMappingMode;
    float exposure = settings.exposure;
    uint8_t autoExposureEnabled = settings.autoExposureEnabled;
    uint8_t autoSPPEnabled = settings.autoSPPEnabled;
    vec3 environmentColor = {
        settings.environmentColor[0],
        settings.environmentColor[1],
        settings.environmentColor[2],
    };
    float environmentStrength = settings.environmentStrength;
    uint32_t debugMode = settings.debugMode;
    uint8_t misNeeEnabled = (uint8_t)(settings.misNeeEnabled != 0u);
    uint32_t selectedMeshIndex = settings.selectedMeshIndex;

    if (!jsonReadOptionalFloatArrayField(cameraObject, "position", cameraPosition, 3u) ||
        !jsonReadOptionalFloatArrayField(cameraObject, "target", cameraTarget, 3u) ||
        !jsonReadOptionalFloatArrayField(cameraObject, "up", cameraUp, 3u) ||
        !jsonReadOptionalFloatField(cameraObject, "vfov", &vfov) ||
        !jsonReadOptionalUInt32Field(settingsObject, "samplesPerPixel", &samplesPerPixel) ||
        !jsonReadOptionalUInt32Field(settingsObject, "rrMinDepth", &rrMinDepth) ||
        !jsonReadOptionalUInt32Field(settingsObject, "rrMaxDepth", &rrMaxDepth) ||
        !jsonReadOptionalUInt32Field(settingsObject, "toneMappingMode", &toneMappingMode) ||
        !jsonReadOptionalFloatField(settingsObject, "exposure", &exposure) ||
        !jsonReadOptionalBoolField(settingsObject, "autoExposureEnabled", &autoExposureEnabled) ||
        !jsonReadOptionalBoolField(settingsObject, "autoSPPEnabled", &autoSPPEnabled) ||
        !jsonReadOptionalFloatArrayField(settingsObject, "environmentColor", environmentColor, 3u) ||
        !jsonReadOptionalFloatField(settingsObject, "environmentStrength", &environmentStrength) ||
        !jsonReadOptionalUInt32Field(settingsObject, "debugMode", &debugMode) ||
        !jsonReadOptionalBoolField(settingsObject, "misNeeEnabled", &misNeeEnabled) ||
        !jsonReadOptionalIndexField(settingsObject, "selectedMeshIndex", &selectedMeshIndex)) {
        return 0;
    }

    if (selectedMeshIndex != VKRT_INVALID_INDEX) {
        if (selectedMeshIndex >= savedMeshCount || !savedToLoadedMeshIndex) return 0;
        selectedMeshIndex = savedToLoadedMeshIndex[selectedMeshIndex];
    }

    return VKRT_setSamplesPerPixel(vkrt, samplesPerPixel) == VKRT_SUCCESS &&
        VKRT_setPathDepth(vkrt, rrMinDepth, rrMaxDepth) == VKRT_SUCCESS &&
        VKRT_setToneMappingMode(vkrt, toneMappingMode) == VKRT_SUCCESS &&
        VKRT_setExposure(vkrt, exposure) == VKRT_SUCCESS &&
        VKRT_setAutoExposureEnabled(vkrt, autoExposureEnabled) == VKRT_SUCCESS &&
        VKRT_setAutoSPPEnabled(vkrt, autoSPPEnabled) == VKRT_SUCCESS &&
        VKRT_setEnvironmentLight(vkrt, environmentColor, environmentStrength) == VKRT_SUCCESS &&
        VKRT_setDebugMode(vkrt, debugMode) == VKRT_SUCCESS &&
        VKRT_setMISNEEEnabled(vkrt, misNeeEnabled ? 1u : 0u) == VKRT_SUCCESS &&
        VKRT_setSceneTimeline(vkrt, NULL) == VKRT_SUCCESS &&
        VKRT_cameraSetPose(vkrt, cameraPosition, cameraTarget, cameraUp, vfov) == VKRT_SUCCESS &&
        VKRT_setSelectedMesh(vkrt, selectedMeshIndex) == VKRT_SUCCESS;
}

static int queryHighestSavedMaterialIndex(const cJSON* materialsArray, const cJSON* meshesArray, uint32_t* outHighestIndex) {
    if (outHighestIndex) *outHighestIndex = 0u;
    if (!meshesArray || !cJSON_IsArray(meshesArray) || !outHighestIndex) {
        return 0;
    }
    if (materialsArray && !cJSON_IsArray(materialsArray)) return 0;

    uint32_t highestIndex = 0u;
    uint32_t contiguousIndex = 0u;
    cJSON* materialObject = NULL;
    if (materialsArray) {
        cJSON_ArrayForEach(materialObject, (cJSON*)materialsArray) {
            uint32_t materialIndex = contiguousIndex;
            const cJSON* explicitIndex = cJSON_GetObjectItemCaseSensitive(materialObject, "index");
            if (explicitIndex && !jsonToUInt32(explicitIndex, &materialIndex)) {
                return 0;
            }
            if (materialIndex > highestIndex) highestIndex = materialIndex;
            contiguousIndex++;
        }
    }

    cJSON* meshObject = NULL;
    cJSON_ArrayForEach(meshObject, (cJSON*)meshesArray) {
        uint32_t materialIndex = 0u;
        if (!jsonToUInt32(cJSON_GetObjectItemCaseSensitive(meshObject, "materialIndex"), &materialIndex)) {
            return 0;
        }
        if (materialIndex > highestIndex) highestIndex = materialIndex;
    }

    *outHighestIndex = highestIndex;
    return 1;
}

static int ensureMaterialCapacityForSceneLoad(VKRT* vkrt, uint32_t requiredCount) {
    if (!vkrt) return 0;

    uint32_t materialCount = 0u;
    if (VKRT_getMaterialCount(vkrt, &materialCount) != VKRT_SUCCESS) return 0;
    while (materialCount < requiredCount) {
        if (VKRT_addMaterial(vkrt, NULL, NULL, NULL) != VKRT_SUCCESS) {
            return 0;
        }
        materialCount++;
    }
    return 1;
}

static cJSON* createSceneJSON(VKRT* vkrt, Session* session, const char* scenePath) {
    if (!vkrt || !session) return NULL;

    VKRT_SceneSettingsSnapshot settings = {0};
    uint32_t meshCount = 0u;
    uint32_t materialCount = 0u;
    if (VKRT_getSceneSettings(vkrt, &settings) != VKRT_SUCCESS ||
        VKRT_getMeshCount(vkrt, &meshCount) != VKRT_SUCCESS ||
        VKRT_getMaterialCount(vkrt, &materialCount) != VKRT_SUCCESS ||
        sessionGetMeshRecordCount(session) != meshCount) {
        return NULL;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "format", "vkrt.scene");
    cJSON_AddNumberToObject(root, "version", kSceneFileVersion);

    cJSON* meshImportsArray = cJSON_CreateArray();
    cJSON* textureImportsArray = cJSON_CreateArray();
    cJSON* materialsArray = cJSON_CreateArray();
    cJSON* meshesArray = cJSON_CreateArray();
    cJSON* sceneObjects = cJSON_CreateArray();
    if (!meshImportsArray || !textureImportsArray || !materialsArray || !meshesArray || !sceneObjects) {
        cJSON_Delete(meshImportsArray);
        cJSON_Delete(textureImportsArray);
        cJSON_Delete(materialsArray);
        cJSON_Delete(meshesArray);
        cJSON_Delete(sceneObjects);
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(root, "meshImports", meshImportsArray);
    cJSON_AddItemToObject(root, "textureImports", textureImportsArray);
    cJSON_AddItemToObject(root, "materials", materialsArray);
    cJSON_AddItemToObject(root, "meshes", meshesArray);
    cJSON_AddItemToObject(root, "sceneObjects", sceneObjects);

    if (!saveSceneSettings(root, &settings)) {
        cJSON_Delete(root);
        return NULL;
    }

    if (sessionGetEnvironmentTexturePath(session)[0]) {
        char storedPath[VKRT_PATH_MAX];
        if (!copyPortableStoredPath(scenePath, sessionGetEnvironmentTexturePath(session), storedPath)) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddStringToObject(root, "environmentTexturePath", storedPath);
    }

    for (uint32_t i = 1u; i < materialCount; i++) {
        VKRT_MaterialSnapshot material = {0};
        if (VKRT_getMaterialSnapshot(vkrt, i, &material) != VKRT_SUCCESS) {
            cJSON_Delete(root);
            return NULL;
        }

        cJSON* materialObject = cJSON_CreateObject();
        cJSON* materialJSON = createMaterialJSON(&material.material);
        if (!materialObject || !materialJSON) {
            cJSON_Delete(materialObject);
            cJSON_Delete(materialJSON);
            cJSON_Delete(root);
            return NULL;
        }

        cJSON_AddNumberToObject(materialObject, "index", i);
        cJSON_AddStringToObject(materialObject, "name", material.name);
        cJSON_AddItemToObject(materialObject, "material", materialJSON);
        cJSON_AddItemToArray(materialsArray, materialObject);
    }

    uint32_t* importBatchIndices = meshCount > 0u ? (uint32_t*)malloc((size_t)meshCount * sizeof(*importBatchIndices)) : NULL;
    uint32_t importBatchCount = 0u;
    if (meshCount > 0u && !importBatchIndices) {
        cJSON_Delete(root);
        return NULL;
    }

    for (uint32_t i = 0; i < meshCount; i++) {
        const SessionMeshRecord* meshRecord = sessionGetMeshRecord(session, i);
        VKRT_MeshSnapshot mesh = {0};
        if (!meshRecord || VKRT_getMeshSnapshot(vkrt, i, &mesh) != VKRT_SUCCESS) {
            free(importBatchIndices);
            cJSON_Delete(root);
            return NULL;
        }

        uint32_t importIndex = VKRT_INVALID_INDEX;
        for (uint32_t importCandidate = 0u; importCandidate < importBatchCount; importCandidate++) {
            if (importBatchIndices[importCandidate] == meshRecord->importBatchIndex) {
                importIndex = importCandidate;
                break;
            }
        }
        if (importIndex == VKRT_INVALID_INDEX) {
            const char* batchPath = sessionGetMeshImportPath(session, meshRecord->importBatchIndex);
            char storedPath[VKRT_PATH_MAX];
            if (!batchPath || !batchPath[0] || !copyPortableStoredPath(scenePath, batchPath, storedPath)) {
                free(importBatchIndices);
                cJSON_Delete(root);
                return NULL;
            }
            importIndex = importBatchCount;
            importBatchIndices[importBatchCount++] = meshRecord->importBatchIndex;
            cJSON_AddItemToArray(meshImportsArray, cJSON_CreateString(storedPath));
        }

        cJSON* meshObject = cJSON_CreateObject();
        if (!meshObject) {
            free(importBatchIndices);
            cJSON_Delete(root);
            return NULL;
        }

        cJSON_AddStringToObject(meshObject, "name", mesh.name);
        cJSON_AddNumberToObject(meshObject, "materialIndex", mesh.materialIndex);
        cJSON_AddBoolToObject(meshObject, "hasMaterialAssignment", mesh.hasMaterialAssignment != 0u);
        cJSON_AddBoolToObject(meshObject, "renderBackfaces", mesh.info.renderBackfaces != 0u);
        cJSON_AddNumberToObject(meshObject, "opacity", mesh.info.opacity);
        cJSON_AddNumberToObject(meshObject, "importIndex", importIndex);
        cJSON_AddNumberToObject(meshObject, "importLocalIndex", meshRecord->importLocalIndex);
        cJSON_AddItemToArray(meshesArray, meshObject);
    }
    free(importBatchIndices);

    for (uint32_t i = 0; i < sessionGetTextureRecordCount(session); i++) {
        const SessionTextureRecord* texture = sessionGetTextureRecord(session, i);
        if (!texture || !texture->sourcePath || !texture->sourcePath[0]) continue;

        char storedPath[VKRT_PATH_MAX];
        if (!copyPortableStoredPath(scenePath, texture->sourcePath, storedPath)) {
            cJSON_Delete(root);
            return NULL;
        }

        cJSON* textureObject = cJSON_CreateObject();
        if (!textureObject) {
            cJSON_Delete(root);
            return NULL;
        }

        cJSON_AddNumberToObject(textureObject, "index", i);
        cJSON_AddNumberToObject(textureObject, "colorSpace", texture->colorSpace);
        cJSON_AddStringToObject(textureObject, "path", storedPath);
        cJSON_AddItemToArray(textureImportsArray, textureObject);
    }

    for (uint32_t i = 0; i < sessionGetSceneObjectCount(session); i++) {
        const SessionSceneObject* object = sessionGetSceneObject(session, i);
        if (!object) continue;

        cJSON* objectJSON = cJSON_CreateObject();
        if (!objectJSON) {
            cJSON_Delete(root);
            return NULL;
        }

        cJSON_AddStringToObject(objectJSON, "name", object->name);
        addUIntOrNull(objectJSON, "parentIndex", object->parentIndex);
        addUIntOrNull(objectJSON, "meshIndex", object->meshIndex);
        if (!addSceneObjectTransformJSON(objectJSON, object)) {
            cJSON_Delete(objectJSON);
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddItemToArray(sceneObjects, objectJSON);
    }

    return root;
}

static int sceneControllerSaveScene(VKRT* vkrt, Session* session, const char* path) {
    if (!vkrt || !session || !path || !path[0]) return 0;

    cJSON* root = createSceneJSON(vkrt, session, path);
    if (!root) return 0;

    char* jsonText = cJSON_Print(root);
    cJSON_Delete(root);
    if (!jsonText) return 0;

    FILE* file = openFileForWrite(path);
    if (!file) {
        cJSON_free(jsonText);
        return 0;
    }

    size_t length = strlen(jsonText);
    size_t written = fwrite(jsonText, 1, length, file);
    fclose(file);
    cJSON_free(jsonText);
    if (written != length) return 0;

    sessionSetCurrentScenePath(session, path);
    return 1;
}

static int loadSceneDocument(VKRT* vkrt, Session* session, cJSON* root, const char* path, const char* targetScenePath) {
    if (!vkrt || !session || !root) {
        cJSON_Delete(root);
        return 0;
    }

    const cJSON* format = cJSON_GetObjectItemCaseSensitive(root, "format");
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON* meshImportsArray = cJSON_GetObjectItemCaseSensitive(root, "meshImports");
    const cJSON* textureImportsArray = cJSON_GetObjectItemCaseSensitive(root, "textureImports");
    const cJSON* materialsArray = cJSON_GetObjectItemCaseSensitive(root, "materials");
    const cJSON* meshesArray = cJSON_GetObjectItemCaseSensitive(root, "meshes");
    const cJSON* sceneObjectsArray = cJSON_GetObjectItemCaseSensitive(root, "sceneObjects");
    const cJSON* settingsObject = cJSON_GetObjectItemCaseSensitive(root, "sceneSettings");
    const cJSON* environmentTexturePath = cJSON_GetObjectItemCaseSensitive(root, "environmentTexturePath");
    uint32_t fileVersion = 0u;
    if (!cJSON_IsString(format) || strcmp(format->valuestring, "vkrt.scene") != 0 ||
        !jsonToUInt32(version, &fileVersion) || fileVersion != kSceneFileVersion ||
        !cJSON_IsArray(meshImportsArray) ||
        (textureImportsArray && !cJSON_IsArray(textureImportsArray)) ||
        !cJSON_IsArray(meshesArray) || !cJSON_IsArray(sceneObjectsArray) ||
        !cJSON_IsObject(settingsObject) ||
        (materialsArray && !cJSON_IsArray(materialsArray)) ||
        (environmentTexturePath && !cJSON_IsString(environmentTexturePath) && !cJSON_IsNull(environmentTexturePath))) {
        cJSON_Delete(root);
        return 0;
    }

    uint32_t savedMeshCount = (uint32_t)cJSON_GetArraySize((cJSON*)meshesArray);
    uint32_t textureIndexMapCount = 0u;
    if (!querySavedTextureIndexMapCount(textureImportsArray, &textureIndexMapCount)) {
        cJSON_Delete(root);
        return 0;
    }
    uint32_t* textureIndexMap = textureIndexMapCount > 0u
        ? (uint32_t*)malloc((size_t)textureIndexMapCount * sizeof(uint32_t))
        : NULL;
    uint32_t* savedToLoadedMeshIndex = savedMeshCount > 0u
        ? (uint32_t*)malloc((size_t)savedMeshCount * sizeof(uint32_t))
        : NULL;
    uint8_t* keepMesh = NULL;
    LoadedMeshImportBatch* loadedBatches = NULL;
    uint32_t loadedBatchCount = 0u;

    if ((textureIndexMapCount > 0u && !textureIndexMap) || (savedMeshCount > 0u && !savedToLoadedMeshIndex)) {
        free(textureIndexMap);
        free(savedToLoadedMeshIndex);
        cJSON_Delete(root);
        return 0;
    }

    for (uint32_t i = 0; i < textureIndexMapCount; i++) {
        textureIndexMap[i] = VKRT_INVALID_INDEX;
    }
    for (uint32_t i = 0; i < savedMeshCount; i++) {
        savedToLoadedMeshIndex[i] = VKRT_INVALID_INDEX;
    }

    sessionSetCurrentScenePath(session, NULL);
    if (!clearCurrentScene(vkrt, session)) {
        free(textureIndexMap);
        free(savedToLoadedMeshIndex);
        cJSON_Delete(root);
        return 0;
    }

    cJSON* meshImport = NULL;
    cJSON_ArrayForEach(meshImport, (cJSON*)meshImportsArray) {
        char resolvedPath[VKRT_PATH_MAX];
        if (!meshImport || !cJSON_IsString(meshImport) ||
            !resolveSceneAssetPath(path, meshImport->valuestring, resolvedPath)) {
            loadedBatchCount = UINT32_MAX;
            break;
        }

        uint32_t meshCountBefore = 0u;
        uint32_t meshCountAfter = 0u;
        if (VKRT_getMeshCount(vkrt, &meshCountBefore) != VKRT_SUCCESS ||
            !meshControllerImportMesh(vkrt, session, resolvedPath, NULL, NULL) ||
            VKRT_getMeshCount(vkrt, &meshCountAfter) != VKRT_SUCCESS ||
            meshCountAfter < meshCountBefore) {
            loadedBatchCount = UINT32_MAX;
            break;
        }

        LoadedMeshImportBatch* resized = (LoadedMeshImportBatch*)realloc(
            loadedBatches,
            (size_t)(loadedBatchCount + 1u) * sizeof(*loadedBatches)
        );
        if (!resized) {
            loadedBatchCount = UINT32_MAX;
            break;
        }
        loadedBatches = resized;
        loadedBatches[loadedBatchCount++] = (LoadedMeshImportBatch){
            .firstMeshIndex = meshCountBefore,
            .meshCount = meshCountAfter - meshCountBefore,
        };
    }

    if (loadedBatchCount == UINT32_MAX) {
        free(textureIndexMap);
        free(savedToLoadedMeshIndex);
        free(loadedBatches);
        cJSON_Delete(root);
        return 0;
    }

    if (textureImportsArray) {
        cJSON* textureObject = NULL;
        cJSON_ArrayForEach(textureObject, (cJSON*)textureImportsArray) {
            const cJSON* pathItem = cJSON_GetObjectItemCaseSensitive(textureObject, "path");
            uint32_t savedTextureIndex = 0u;
            uint32_t colorSpace = 0u;
            char resolvedPath[VKRT_PATH_MAX];
            uint32_t newTextureIndex = VKRT_INVALID_INDEX;
            if (!textureObject || !cJSON_IsObject(textureObject) ||
                !pathItem || !cJSON_IsString(pathItem) ||
                !jsonToUInt32(cJSON_GetObjectItemCaseSensitive(textureObject, "index"), &savedTextureIndex) ||
                !jsonToUInt32(cJSON_GetObjectItemCaseSensitive(textureObject, "colorSpace"), &colorSpace) ||
                !resolveSceneAssetPath(path, pathItem->valuestring, resolvedPath) ||
                VKRT_addTextureFromFile(vkrt, resolvedPath, NULL, colorSpace, &newTextureIndex) != VKRT_SUCCESS ||
                !sessionAppendStandaloneTextureRecord(session, resolvedPath, colorSpace) ||
                savedTextureIndex >= textureIndexMapCount) {
                loadedBatchCount = UINT32_MAX;
                break;
            }
            textureIndexMap[savedTextureIndex] = newTextureIndex;
        }
    }

    if (loadedBatchCount == UINT32_MAX) {
        free(textureIndexMap);
        free(savedToLoadedMeshIndex);
        free(loadedBatches);
        cJSON_Delete(root);
        return 0;
    }

    if (environmentTexturePath && cJSON_IsString(environmentTexturePath)) {
        char resolvedEnvironmentPath[VKRT_PATH_MAX];
        if (!resolveSceneAssetPath(path, environmentTexturePath->valuestring, resolvedEnvironmentPath) ||
            VKRT_setEnvironmentTextureFromFile(vkrt, resolvedEnvironmentPath) != VKRT_SUCCESS) {
            free(textureIndexMap);
            free(savedToLoadedMeshIndex);
            free(loadedBatches);
            cJSON_Delete(root);
            return 0;
        }
        sessionSetEnvironmentTexturePath(session, resolvedEnvironmentPath);
    }

    cJSON* meshObject = NULL;
    uint32_t savedMeshIndex = 0u;
    cJSON_ArrayForEach(meshObject, (cJSON*)meshesArray) {
        uint32_t importIndex = 0u;
        uint32_t importLocalIndex = 0u;
        if (!jsonToUInt32(cJSON_GetObjectItemCaseSensitive(meshObject, "importIndex"), &importIndex) ||
            !jsonToUInt32(cJSON_GetObjectItemCaseSensitive(meshObject, "importLocalIndex"), &importLocalIndex)) {
            savedMeshIndex = UINT32_MAX;
            break;
        }

        if (importIndex >= loadedBatchCount || importLocalIndex >= loadedBatches[importIndex].meshCount) {
            savedMeshIndex = UINT32_MAX;
            break;
        }
        savedToLoadedMeshIndex[savedMeshIndex++] = loadedBatches[importIndex].firstMeshIndex + importLocalIndex;
    }

    if (savedMeshIndex == UINT32_MAX) {
        free(textureIndexMap);
        free(savedToLoadedMeshIndex);
        free(loadedBatches);
        cJSON_Delete(root);
        return 0;
    }

    uint32_t currentMeshCount = 0u;
    if (VKRT_getMeshCount(vkrt, &currentMeshCount) != VKRT_SUCCESS) {
        free(textureIndexMap);
        free(savedToLoadedMeshIndex);
        free(loadedBatches);
        cJSON_Delete(root);
        return 0;
    }

    keepMesh = currentMeshCount > 0u ? (uint8_t*)calloc(currentMeshCount, sizeof(uint8_t)) : NULL;
    if (currentMeshCount > 0u && !keepMesh) {
        free(textureIndexMap);
        free(savedToLoadedMeshIndex);
        free(loadedBatches);
        cJSON_Delete(root);
        return 0;
    }

    for (uint32_t i = 0; i < savedMeshCount; i++) {
        if (savedToLoadedMeshIndex[i] >= currentMeshCount) {
            free(textureIndexMap);
            free(savedToLoadedMeshIndex);
            free(loadedBatches);
            free(keepMesh);
            cJSON_Delete(root);
            return 0;
        }
        keepMesh[savedToLoadedMeshIndex[i]] = 1u;
    }

    sessionTruncateSceneObjects(session, 0u);
    sessionSetSelectedSceneObject(session, VKRT_INVALID_INDEX);
    for (uint32_t meshIndex = currentMeshCount; meshIndex > 0u; meshIndex--) {
        uint32_t removeIndex = meshIndex - 1u;
        if (keepMesh[removeIndex]) continue;
        if (VKRT_removeMesh(vkrt, removeIndex) != VKRT_SUCCESS) {
            free(textureIndexMap);
            free(savedToLoadedMeshIndex);
            free(loadedBatches);
            free(keepMesh);
            cJSON_Delete(root);
            return 0;
        }
        sessionRemoveMeshRecord(session, removeIndex);
        sessionRemoveMeshReferences(session, removeIndex);
        for (uint32_t i = 0; i < savedMeshCount; i++) {
            if (savedToLoadedMeshIndex[i] != VKRT_INVALID_INDEX && savedToLoadedMeshIndex[i] > removeIndex) {
                savedToLoadedMeshIndex[i]--;
            }
        }
    }

    uint32_t highestSavedMaterialIndex = 0u;
    if (!queryHighestSavedMaterialIndex(materialsArray, meshesArray, &highestSavedMaterialIndex) ||
        !ensureMaterialCapacityForSceneLoad(vkrt, highestSavedMaterialIndex + 1u)) {
        free(textureIndexMap);
        free(savedToLoadedMeshIndex);
        free(loadedBatches);
        free(keepMesh);
        cJSON_Delete(root);
        return 0;
    }

    cJSON* materialObject = NULL;
    uint32_t contiguousMaterialIndex = 0u;
    if (materialsArray) {
        cJSON_ArrayForEach(materialObject, (cJSON*)materialsArray) {
            const cJSON* name = cJSON_GetObjectItemCaseSensitive(materialObject, "name");
            const cJSON* materialValue = cJSON_GetObjectItemCaseSensitive(materialObject, "material");
            const cJSON* explicitIndex = cJSON_GetObjectItemCaseSensitive(materialObject, "index");
            uint32_t materialIndex = contiguousMaterialIndex;
            Material material = VKRT_materialDefault();
            if ((explicitIndex && !jsonToUInt32(explicitIndex, &materialIndex)) ||
                !name || !cJSON_IsString(name) || !parseMaterialJSON(materialValue, &material)) {
                contiguousMaterialIndex = UINT32_MAX;
                break;
            }

            remapStandaloneTextureIndices(&material, textureIndexMap, textureIndexMapCount);
            if (VKRT_setMaterial(vkrt, materialIndex, &material) != VKRT_SUCCESS ||
                VKRT_setMaterialName(vkrt, materialIndex, name->valuestring) != VKRT_SUCCESS) {
                contiguousMaterialIndex = UINT32_MAX;
                break;
            }
            contiguousMaterialIndex++;
        }
    }

    if (contiguousMaterialIndex == UINT32_MAX) {
        free(textureIndexMap);
        free(savedToLoadedMeshIndex);
        free(loadedBatches);
        free(keepMesh);
        cJSON_Delete(root);
        return 0;
    }

    meshObject = NULL;
    savedMeshIndex = 0u;
    cJSON_ArrayForEach(meshObject, (cJSON*)meshesArray) {
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(meshObject, "name");
        const cJSON* hasMaterialAssignment = cJSON_GetObjectItemCaseSensitive(meshObject, "hasMaterialAssignment");
        const cJSON* renderBackfaces = cJSON_GetObjectItemCaseSensitive(meshObject, "renderBackfaces");
        uint32_t materialRef = 0u;
        uint8_t assigned = 0u;
        uint8_t backfaces = 0u;
        float opacity = 1.0f;
        uint32_t loadedMeshIndex = savedToLoadedMeshIndex[savedMeshIndex++];
        if (!name || !cJSON_IsString(name) ||
            !jsonToUInt32(cJSON_GetObjectItemCaseSensitive(meshObject, "materialIndex"), &materialRef) ||
            !jsonToBool(hasMaterialAssignment, &assigned) ||
            !jsonToBool(renderBackfaces, &backfaces) ||
            !jsonToFloat(cJSON_GetObjectItemCaseSensitive(meshObject, "opacity"), &opacity) ||
            VKRT_setMeshName(vkrt, loadedMeshIndex, name->valuestring) != VKRT_SUCCESS ||
            (assigned
                ? VKRT_setMeshMaterialIndex(vkrt, loadedMeshIndex, materialRef)
                : VKRT_clearMeshMaterialAssignment(vkrt, loadedMeshIndex)) != VKRT_SUCCESS ||
            VKRT_setMeshOpacity(vkrt, loadedMeshIndex, opacity) != VKRT_SUCCESS ||
            VKRT_setMeshRenderBackfaces(vkrt, loadedMeshIndex, backfaces ? 1u : 0u) != VKRT_SUCCESS) {
            savedMeshIndex = UINT32_MAX;
            break;
        }
    }

    if (savedMeshIndex == UINT32_MAX) {
        free(textureIndexMap);
        free(savedToLoadedMeshIndex);
        free(loadedBatches);
        free(keepMesh);
        cJSON_Delete(root);
        return 0;
    }

    cJSON* sceneObject = NULL;
    uint32_t sceneObjectIndex = 0u;
    cJSON_ArrayForEach(sceneObject, (cJSON*)sceneObjectsArray) {
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(sceneObject, "name");
        uint32_t parentIndex = VKRT_INVALID_INDEX;
        uint32_t savedMeshRef = VKRT_INVALID_INDEX;
        vec3 localPosition = GLM_VEC3_ZERO_INIT;
        vec3 localRotation = GLM_VEC3_ZERO_INIT;
        vec3 localScale = {1.0f, 1.0f, 1.0f};
        mat4 localTransform = GLM_MAT4_IDENTITY_INIT;
        uint8_t useMatrix = 0u;
        vec3 zero = GLM_VEC3_ZERO_INIT;
        vec3 one = {1.0f, 1.0f, 1.0f};
        uint32_t loadedMeshIndex = VKRT_INVALID_INDEX;
        if (!name || !cJSON_IsString(name) ||
            !jsonToIndexOrInvalid(cJSON_GetObjectItemCaseSensitive(sceneObject, "parentIndex"), &parentIndex) ||
            !jsonToIndexOrInvalid(cJSON_GetObjectItemCaseSensitive(sceneObject, "meshIndex"), &savedMeshRef) ||
            !parseSceneObjectTransformJSON(sceneObject, localPosition, localRotation, localScale, localTransform, &useMatrix)) {
            sceneObjectIndex = UINT32_MAX;
            break;
        }
        if (parentIndex != VKRT_INVALID_INDEX && parentIndex >= sceneObjectIndex) {
            sceneObjectIndex = UINT32_MAX;
            break;
        }
        if (savedMeshRef != VKRT_INVALID_INDEX) {
            if (savedMeshRef >= savedMeshCount) {
                sceneObjectIndex = UINT32_MAX;
                break;
            }
            loadedMeshIndex = savedToLoadedMeshIndex[savedMeshRef];
        }
        if (!sessionAddSceneObject(
                session,
                name->valuestring,
                parentIndex,
                loadedMeshIndex,
                useMatrix ? zero : localPosition,
                useMatrix ? zero : localRotation,
                useMatrix ? one : localScale,
                NULL
            ) ||
            (useMatrix && !sessionSetSceneObjectLocalTransformMatrix(session, sceneObjectIndex, localTransform))) {
            sceneObjectIndex = UINT32_MAX;
            break;
        }
        sceneObjectIndex++;
    }

    if (sceneObjectIndex == UINT32_MAX ||
        !sessionSyncSceneObjectTransforms(vkrt, session) ||
        !applySceneSettings(vkrt, settingsObject, savedToLoadedMeshIndex, savedMeshCount)) {
        free(textureIndexMap);
        free(savedToLoadedMeshIndex);
        free(loadedBatches);
        free(keepMesh);
        cJSON_Delete(root);
        return 0;
    }

    sessionSetCurrentScenePath(session, targetScenePath && targetScenePath[0] ? targetScenePath : NULL);
    free(textureIndexMap);
    free(savedToLoadedMeshIndex);
    free(loadedBatches);
    free(keepMesh);
    cJSON_Delete(root);
    return 1;
}

static int sceneControllerLoadScene(VKRT* vkrt, Session* session, const char* path) {
    if (!vkrt || !session || !path || !path[0]) return 0;

    char* jsonText = NULL;
    if (!readTextFile(path, &jsonText) || !jsonText) {
        return 0;
    }

    cJSON* root = cJSON_Parse(jsonText);
    free(jsonText);
    if (!root) return 0;

    char* previousScenePath = stringDuplicate(sessionGetCurrentScenePath(session));
    if (!previousScenePath) {
        cJSON_Delete(root);
        return 0;
    }

    uint32_t meshCount = 0u;
    uint32_t materialCount = 0u;
    uint32_t textureCount = 0u;
    int canBackupState =
        VKRT_getMeshCount(vkrt, &meshCount) == VKRT_SUCCESS &&
        VKRT_getMaterialCount(vkrt, &materialCount) == VKRT_SUCCESS &&
        VKRT_getTextureCount(vkrt, &textureCount) == VKRT_SUCCESS;
    int haveSceneState = canBackupState && (
        meshCount > 0u ||
        materialCount > 1u ||
        textureCount > 0u ||
        sessionGetSceneObjectCount(session) > 0u ||
        sessionGetEnvironmentTexturePath(session)[0] ||
        previousScenePath[0]
    );

    cJSON* backupRoot = NULL;
    if (haveSceneState) {
        backupRoot = createSceneJSON(vkrt, session, previousScenePath[0] ? previousScenePath : NULL);
        if (!backupRoot) {
            free(previousScenePath);
            cJSON_Delete(root);
            return 0;
        }
    }

    if (loadSceneDocument(vkrt, session, root, path, path)) {
        cJSON_Delete(backupRoot);
        free(previousScenePath);
        return 1;
    }

    if (backupRoot) {
        if (!loadSceneDocument(
                vkrt,
                session,
                backupRoot,
                previousScenePath[0] ? previousScenePath : NULL,
                previousScenePath[0] ? previousScenePath : NULL
            )) {
            LOG_ERROR("Restoring previous scene after load failure failed");
            if (!clearCurrentScene(vkrt, session)) {
                LOG_ERROR("Clearing partial scene after load failure failed");
            }
        }
    } else if (!clearCurrentScene(vkrt, session)) {
        LOG_ERROR("Clearing partial scene after load failure failed");
    }

    free(previousScenePath);
    return 0;
}

int sceneControllerLoadDefaultScene(VKRT* vkrt, Session* session) {
    char resolvedPath[VKRT_PATH_MAX];
    if (resolveExistingPath(kDefaultScenePath, resolvedPath, sizeof(resolvedPath)) != 0) {
        return 0;
    }
    return sceneControllerLoadScene(vkrt, session, resolvedPath);
}

void sceneControllerApplySessionActions(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    char* openScenePath = NULL;
    if (sessionTakeSceneOpen(session, &openScenePath)) {
        if (!sceneControllerLoadScene(vkrt, session, openScenePath)) {
            LOG_ERROR("Scene load failed. File: %s", openScenePath);
        }
        free(openScenePath);
    }

    char* saveScenePath = NULL;
    if (sessionTakeSceneSave(session, &saveScenePath)) {
        if (!sceneControllerSaveScene(vkrt, session, saveScenePath)) {
            LOG_ERROR("Scene save failed. File: %s", saveScenePath);
        }
        free(saveScenePath);
    }
}
