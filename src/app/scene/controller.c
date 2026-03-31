#include "controller.h"

#include "cJSON.h"
#include "constants.h"
#include "debug.h"
#include "io.h"
#include "mesh/controller.h"
#include "platform.h"
#include "session.h"
#include "vkrt.h"
#include "vkrt_types.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <types.h>

enum {
    K_SCENE_FILE_VERSION = 1,
    K_PATH_COMPONENT_CAPACITY = 128,
};

static const char* kDefaultScenePath = "assets/scenes/cornell.json";

typedef struct LoadedMeshImportBatch {
    uint32_t firstMeshIndex;
    uint32_t meshCount;
} LoadedMeshImportBatch;

typedef struct SceneLoadContext {
    VKRT* vkrt;
    Session* session;
    cJSON* root;
    const cJSON* materialsArray;
    const cJSON* meshesArray;
    const cJSON* sceneObjectsArray;
    const cJSON* settingsObject;
    const char* path;
    const char* targetScenePath;
    uint32_t savedMeshCount;
    uint32_t textureIndexMapCount;
    uint32_t* textureIndexMap;
    uint32_t* savedToLoadedMeshIndex;
    uint8_t* keepMesh;
    LoadedMeshImportBatch* loadedBatches;
    uint32_t loadedBatchCount;
} SceneLoadContext;

static FILE* openFileForWrite(const char* path) {
    if (!path || !path[0]) return NULL;
#ifdef _WIN32
    FILE* file = NULL;
    return fopen_s(&file, path, "wb") == 0 ? file : NULL;
#else
    return fopen(path, "wb");
#endif
}

static int readTextFile(const char* path, char** outText) {
    if (outText) *outText = NULL;
    if (!path || !path[0] || !outText) return 0;

#ifdef _WIN32
    FILE* file = NULL;
    if (fopen_s(&file, path, "rb") != 0) return 0;
#else
    FILE* file = fopen(path, "rb");
    if (!file) return 0;
#endif

    if (fseek(file, 0, SEEK_END) != 0) {
        (void)fclose(file);
        return 0;
    }
    long fileSize = ftell(file);
    if (fileSize < 0 || fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return 0;
    }

    size_t textCapacity = (size_t)fileSize + 1u;
    char* text = (char*)calloc(textCapacity, 1u);
    if (!text) {
        (void)fclose(file);
        return 0;
    }

    size_t bytesRead = fread(text, 1, textCapacity - 1u, file);
    (void)fclose(file);
    if (bytesRead != textCapacity - 1u) {
        free(text);
        return 0;
    }

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
#ifdef _WIN32
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
#ifdef _WIN32
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':') {
        return 2u;
    }
#endif
    return path[0] == '/' ? 1u : 0u;
}

static int pathCharsEqual(char left, char right) {
#ifdef _WIN32
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
    while (path[index] == '/') {
        index++;
    }

    while (path[index]) {
        if (count >= componentCapacity) return UINT32_MAX;
        components[count++] = &path[index];
        while (path[index] && path[index] != '/') {
            index++;
        }
        if (!path[index]) break;
        path[index++] = '\0';
        while (path[index] == '/') {
            index++;
        }
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

static int copyStringToPath(char outPath[VKRT_PATH_MAX], const char* value) {
    if (!outPath || !value) return 0;
    return snprintf(outPath, VKRT_PATH_MAX, "%s", value) < VKRT_PATH_MAX;
}

static int normalizeSceneAndSourcePaths(
    const char* scenePath,
    const char* sourcePath,
    char outSceneNormalized[VKRT_PATH_MAX],
    char outSourceNormalized[VKRT_PATH_MAX]
) {
    char sceneDirectory[VKRT_PATH_MAX];
    if (!copyParentDirectory(scenePath, sceneDirectory) || !copyStringToPath(outSceneNormalized, sceneDirectory) ||
        !copyStringToPath(outSourceNormalized, sourcePath)) {
        return 0;
    }

    normalizePathSeparators(outSceneNormalized);
    normalizePathSeparators(outSourceNormalized);
    return 1;
}

static int pathsShareSameRoot(const char* leftPath, const char* rightPath) {
    size_t leftRootLength = queryPathRootLength(leftPath);
    size_t rightRootLength = queryPathRootLength(rightPath);
    if (leftRootLength != rightRootLength) return 0;

    for (size_t index = 0u; index < leftRootLength; index++) {
        if (!pathCharsEqual(leftPath[index], rightPath[index])) return 0;
    }

    return 1;
}

static int buildRelativeStoredPath(
    char outStoredPath[VKRT_PATH_MAX],
    char sceneNormalized[VKRT_PATH_MAX],
    char sourceNormalized[VKRT_PATH_MAX]
) {
    char* sceneComponents[K_PATH_COMPONENT_CAPACITY];
    char* sourceComponents[K_PATH_COMPONENT_CAPACITY];
    const uint32_t pathComponentCapacity = (uint32_t)(sizeof(sceneComponents) / sizeof(sceneComponents[0]));
    uint32_t sceneComponentCount = splitPathComponents(
        sceneNormalized,
        queryPathRootLength(sceneNormalized),
        sceneComponents,
        pathComponentCapacity
    );
    uint32_t sourceComponentCount = splitPathComponents(
        sourceNormalized,
        queryPathRootLength(sourceNormalized),
        sourceComponents,
        pathComponentCapacity
    );
    if (sceneComponentCount == UINT32_MAX || sourceComponentCount == UINT32_MAX) return 1;

    uint32_t commonComponentCount = 0u;
    while (commonComponentCount < sceneComponentCount && commonComponentCount < sourceComponentCount &&
           pathComponentsEqual(sceneComponents[commonComponentCount], sourceComponents[commonComponentCount])) {
        commonComponentCount++;
    }

    char relativePath[VKRT_PATH_MAX] = {0};
    size_t relativeLength = 0u;
    for (uint32_t index = commonComponentCount; index < sceneComponentCount; index++) {
        if (!appendPathSegment(relativePath, sizeof(relativePath), &relativeLength, "..")) return 1;
    }
    for (uint32_t index = commonComponentCount; index < sourceComponentCount; index++) {
        if (!appendPathSegment(relativePath, sizeof(relativePath), &relativeLength, sourceComponents[index])) {
            return 1;
        }
    }

    if (relativeLength == 0u) return copyStringToPath(outStoredPath, ".");
    return copyStringToPath(outStoredPath, relativePath);
}

static int copyPortableStoredPath(const char* scenePath, const char* sourcePath, char outStoredPath[VKRT_PATH_MAX]) {
    if (!sourcePath || !sourcePath[0] || !outStoredPath) return 0;

    char resolvedSource[VKRT_PATH_MAX];
    const char* sourceForStorage = sourcePath;
    if (!pathIsAbsolute(sourcePath) && resolveExistingPath(sourcePath, resolvedSource, sizeof(resolvedSource)) == 0) {
        sourceForStorage = resolvedSource;
    }

    if (!copyStringToPath(outStoredPath, sourceForStorage)) return 0;
    normalizePathSeparators(outStoredPath);

    if (!scenePath || !scenePath[0] || !pathIsAbsolute(outStoredPath)) return 1;

    char sceneNormalized[VKRT_PATH_MAX];
    char sourceNormalized[VKRT_PATH_MAX];
    if (!normalizeSceneAndSourcePaths(scenePath, outStoredPath, sceneNormalized, sourceNormalized)) return 1;
    if (!pathIsAbsolute(sceneNormalized) || !pathIsAbsolute(sourceNormalized)) return 1;
    if (!pathsShareSameRoot(sceneNormalized, sourceNormalized)) return 1;
    return buildRelativeStoredPath(outStoredPath, sceneNormalized, sourceNormalized);
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

static void addObjectItem(cJSON* targetObject, const char* key, cJSON* childItem) {
    if (!targetObject || !key || !childItem) return;
    cJSON_AddItemToObject(targetObject, key, childItem);
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

static int floatArrayEquals(const float* left, const float* right, size_t count) {
    if (!left || !right) return 0;

    for (size_t i = 0; i < count; i++) {
        if (left[i] != right[i]) return 0;
    }
    return 1;
}

static void addFloatArrayIfDifferent(
    cJSON* object,
    const char* name,
    const float* values,
    const float* defaults,
    size_t count
) {
    if (!object || !name || !values || !defaults) return;
    if (!floatArrayEquals(values, defaults, count)) {
        cJSON_AddItemToObject(object, name, createFloatArray(values, count));
    }
}

static void addFloatIfDifferent(cJSON* object, const char* name, float value, float defaultValue) {
    if (!object || !name) return;
    if (value != defaultValue) cJSON_AddNumberToObject(object, name, value);
}

static void addUIntIfDifferent(cJSON* object, const char* name, uint32_t value, uint32_t defaultValue) {
    if (!object || !name) return;
    if (value != defaultValue) cJSON_AddNumberToObject(object, name, value);
}

static int jsonToMat4(const cJSON* item, mat4 outMatrix) {
    float values[16];
    if (!jsonToFloatArray(item, values, 16u)) return 0;

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

    addFloatArrayIfDifferent(object, "baseColor", material->baseColor, defaults.baseColor, 3u);
    addFloatIfDifferent(object, "metallic", material->metallic, defaults.metallic);
    addFloatIfDifferent(object, "roughness", material->roughness, defaults.roughness);
    addFloatIfDifferent(object, "diffuseRoughness", material->diffuseRoughness, defaults.diffuseRoughness);
    addFloatIfDifferent(object, "subsurface", material->subsurface, defaults.subsurface);
    addFloatIfDifferent(object, "opacity", material->opacity, defaults.opacity);
    addFloatIfDifferent(object, "alphaCutoff", material->alphaCutoff, defaults.alphaCutoff);
    addUIntIfDifferent(object, "alphaMode", material->alphaMode, defaults.alphaMode);

    addFloatIfDifferent(object, "ior", material->ior, defaults.ior);
    addFloatIfDifferent(object, "specular", material->specular, defaults.specular);
    addFloatIfDifferent(object, "specularTint", material->specularTint, defaults.specularTint);
    addFloatIfDifferent(object, "anisotropic", material->anisotropic, defaults.anisotropic);

    addFloatIfDifferent(object, "transmission", material->transmission, defaults.transmission);
    addFloatArrayIfDifferent(object, "attenuationColor", material->attenuationColor, defaults.attenuationColor, 3u);
    addFloatIfDifferent(
        object,
        "absorptionCoefficient",
        material->absorptionCoefficient,
        defaults.absorptionCoefficient
    );

    addFloatIfDifferent(object, "clearcoat", material->clearcoat, defaults.clearcoat);
    addFloatIfDifferent(object, "clearcoatGloss", material->clearcoatGloss, defaults.clearcoatGloss);
    addFloatArrayIfDifferent(object, "sheenTintWeight", material->sheenTintWeight, defaults.sheenTintWeight, 4u);
    addFloatIfDifferent(object, "sheenRoughness", material->sheenRoughness, defaults.sheenRoughness);
    addFloatIfDifferent(object, "abbeNumber", material->abbeNumber, defaults.abbeNumber);

    addFloatIfDifferent(object, "emissionLuminance", material->emissionLuminance, defaults.emissionLuminance);
    addFloatArrayIfDifferent(object, "emissionColor", material->emissionColor, defaults.emissionColor, 3u);

    addFloatIfDifferent(object, "normalTextureScale", material->normalTextureScale, defaults.normalTextureScale);
    if (material->baseColorTextureIndex != defaults.baseColorTextureIndex) {
        addUIntOrNull(object, "baseColorTextureIndex", material->baseColorTextureIndex);
    }
    if (material->metallicRoughnessTextureIndex != defaults.metallicRoughnessTextureIndex) {
        addUIntOrNull(object, "metallicRoughnessTextureIndex", material->metallicRoughnessTextureIndex);
    }
    if (material->normalTextureIndex != defaults.normalTextureIndex) {
        addUIntOrNull(object, "normalTextureIndex", material->normalTextureIndex);
    }
    if (material->emissiveTextureIndex != defaults.emissiveTextureIndex) {
        addUIntOrNull(object, "emissiveTextureIndex", material->emissiveTextureIndex);
    }
    addUIntIfDifferent(object, "baseColorTextureWrap", material->baseColorTextureWrap, defaults.baseColorTextureWrap);
    addUIntIfDifferent(
        object,
        "metallicRoughnessTextureWrap",
        material->metallicRoughnessTextureWrap,
        defaults.metallicRoughnessTextureWrap
    );
    addUIntIfDifferent(object, "normalTextureWrap", material->normalTextureWrap, defaults.normalTextureWrap);
    addUIntIfDifferent(object, "emissiveTextureWrap", material->emissiveTextureWrap, defaults.emissiveTextureWrap);
    addUIntIfDifferent(object, "textureTexcoordSets", material->textureTexcoordSets, defaults.textureTexcoordSets);
    addFloatArrayIfDifferent(
        object,
        "baseColorTextureTransform",
        material->baseColorTextureTransform,
        defaults.baseColorTextureTransform,
        4u
    );
    addFloatArrayIfDifferent(
        object,
        "metallicRoughnessTextureTransform",
        material->metallicRoughnessTextureTransform,
        defaults.metallicRoughnessTextureTransform,
        4u
    );
    addFloatArrayIfDifferent(
        object,
        "normalTextureTransform",
        material->normalTextureTransform,
        defaults.normalTextureTransform,
        4u
    );
    addFloatArrayIfDifferent(
        object,
        "emissiveTextureTransform",
        material->emissiveTextureTransform,
        defaults.emissiveTextureTransform,
        4u
    );
    addFloatArrayIfDifferent(object, "textureRotations", material->textureRotations, defaults.textureRotations, 4u);

    addFloatArrayIfDifferent(object, "eta", material->eta, defaults.eta, 3u);
    addFloatArrayIfDifferent(object, "k", material->k, defaults.k, 3u);

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
        !jsonReadOptionalFloatField(object, "abbeNumber", &material.abbeNumber) ||
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
        !jsonReadOptionalFloatArrayField(
            object,
            "metallicRoughnessTextureTransform",
            material.metallicRoughnessTextureTransform,
            4u
        ) ||
        !jsonReadOptionalFloatArrayField(object, "normalTextureTransform", material.normalTextureTransform, 4u) ||
        !jsonReadOptionalFloatArrayField(object, "emissiveTextureTransform", material.emissiveTextureTransform, 4u) ||
        !jsonReadOptionalFloatArrayField(object, "textureRotations", material.textureRotations, 4u)) {
        return 0;
    }

    *outMaterial = material;
    return 1;
}

static void remapStandaloneTextureIndices(
    Material* material,
    const uint32_t* textureIndexMap,
    uint32_t textureIndexMapCount
) {
    if (!material || !textureIndexMap) return;

    uint32_t* textureIndices[] = {
        &material->baseColorTextureIndex,
        &material->metallicRoughnessTextureIndex,
        &material->normalTextureIndex,
        &material->emissiveTextureIndex,
    };
    const uint32_t textureIndexCount = (uint32_t)(sizeof(textureIndices) / sizeof(textureIndices[0]));

    for (uint32_t i = 0; i < textureIndexCount; i++) {
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
    if (!sceneObject || !cJSON_IsObject(sceneObject) || !outPosition || !outRotation || !outScale || !outMatrix ||
        !outUseMatrix) {
        return 0;
    }

    const cJSON* positionObject = cJSON_GetObjectItemCaseSensitive((cJSON*)sceneObject, "localPosition");
    const cJSON* rotationObject = cJSON_GetObjectItemCaseSensitive((cJSON*)sceneObject, "localRotation");
    const cJSON* scaleObject = cJSON_GetObjectItemCaseSensitive((cJSON*)sceneObject, "localScale");
    if (positionObject || rotationObject || scaleObject) {
        if (!positionObject || !rotationObject || !scaleObject || !jsonToFloatArray(positionObject, outPosition, 3u) ||
            !jsonToFloatArray(rotationObject, outRotation, 3u) || !jsonToFloatArray(scaleObject, outScale, 3u)) {
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

static int saveSceneSettings(cJSON* sceneRoot, const VKRT_SceneSettingsSnapshot* settings) {
    if (!sceneRoot || !settings) return 0;

    cJSON* settingsObject = cJSON_CreateObject();
    if (!settingsObject) return 0;

    cJSON* camera = cJSON_CreateObject();
    if (!camera) {
        cJSON_Delete(settingsObject);
        return 0;
    }
    cJSON_AddItemToObject(camera, "position", createFloatArray(settings->camera.pos, 3u));
    cJSON_AddItemToObject(camera, "target", createFloatArray(settings->camera.target, 3u));
    cJSON_AddItemToObject(camera, "up", createFloatArray(settings->camera.up, 3u));
    cJSON_AddNumberToObject(camera, "vfov", settings->camera.vfov);
    cJSON_AddItemToObject(settingsObject, "camera", camera);
    cJSON_AddNumberToObject(settingsObject, "rrMinDepth", settings->rrMinDepth);
    cJSON_AddNumberToObject(settingsObject, "rrMaxDepth", settings->rrMaxDepth);
    cJSON_AddNumberToObject(settingsObject, "toneMappingMode", settings->toneMappingMode);
    cJSON_AddNumberToObject(settingsObject, "renderMode", settings->renderMode);
    cJSON_AddNumberToObject(settingsObject, "spectralSamplingMode", settings->spectralSamplingMode);
    cJSON_AddNumberToObject(settingsObject, "exposure", settings->exposure);
    cJSON_AddBoolToObject(settingsObject, "autoExposureEnabled", settings->autoExposureEnabled != 0u);
    cJSON_AddItemToObject(settingsObject, "environmentColor", createFloatArray(settings->environmentColor, 3u));
    cJSON_AddNumberToObject(settingsObject, "environmentStrength", settings->environmentStrength);
    cJSON_AddNumberToObject(settingsObject, "environmentRotation", settings->environmentRotation);
    cJSON_AddBoolToObject(settingsObject, "misNeeEnabled", settings->misNeeEnabled != 0u);

    addObjectItem(sceneRoot, "sceneSettings", settingsObject);
    return 1;
}

static int querySceneHasBackupState(
    VKRT* vkrt,
    Session* session,
    const char* previousScenePath,
    uint32_t* outMeshCount,
    uint32_t* outMaterialCount,
    uint32_t* outTextureCount
) {
    if (!vkrt || !session || !outMeshCount || !outMaterialCount || !outTextureCount) return 0;
    if (VKRT_getMeshCount(vkrt, outMeshCount) != VKRT_SUCCESS ||
        VKRT_getMaterialCount(vkrt, outMaterialCount) != VKRT_SUCCESS ||
        VKRT_getTextureCount(vkrt, outTextureCount) != VKRT_SUCCESS) {
        return 0;
    }

    return *outMeshCount > 0u || *outMaterialCount > 1u || *outTextureCount > 0u ||
           sessionGetSceneObjectCount(session) > 0u || sessionGetEnvironmentTexturePath(session)[0] ||
           (previousScenePath && previousScenePath[0]);
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
    uint32_t rrMinDepth = settings.rrMinDepth;
    uint32_t rrMaxDepth = settings.rrMaxDepth;
    uint32_t toneMappingMode = settings.toneMappingMode;
    uint32_t renderMode = settings.renderMode;
    uint32_t spectralSamplingMode = settings.spectralSamplingMode;
    float exposure = settings.exposure;
    uint8_t autoExposureEnabled = settings.autoExposureEnabled;
    vec3 environmentColor = {
        settings.environmentColor[0],
        settings.environmentColor[1],
        settings.environmentColor[2],
    };
    float environmentStrength = settings.environmentStrength;
    float environmentRotation = settings.environmentRotation;
    uint8_t misNeeEnabled = (uint8_t)(settings.misNeeEnabled != 0u);

    if (!jsonReadOptionalFloatArrayField(cameraObject, "position", cameraPosition, 3u) ||
        !jsonReadOptionalFloatArrayField(cameraObject, "target", cameraTarget, 3u) ||
        !jsonReadOptionalFloatArrayField(cameraObject, "up", cameraUp, 3u) ||
        !jsonReadOptionalFloatField(cameraObject, "vfov", &vfov) ||
        !jsonReadOptionalUInt32Field(settingsObject, "rrMinDepth", &rrMinDepth) ||
        !jsonReadOptionalUInt32Field(settingsObject, "rrMaxDepth", &rrMaxDepth) ||
        !jsonReadOptionalUInt32Field(settingsObject, "toneMappingMode", &toneMappingMode) ||
        !jsonReadOptionalUInt32Field(settingsObject, "renderMode", &renderMode) ||
        !jsonReadOptionalUInt32Field(settingsObject, "spectralSamplingMode", &spectralSamplingMode) ||
        !jsonReadOptionalFloatField(settingsObject, "exposure", &exposure) ||
        !jsonReadOptionalBoolField(settingsObject, "autoExposureEnabled", &autoExposureEnabled) ||
        !jsonReadOptionalFloatArrayField(settingsObject, "environmentColor", environmentColor, 3u) ||
        !jsonReadOptionalFloatField(settingsObject, "environmentStrength", &environmentStrength) ||
        !jsonReadOptionalFloatField(settingsObject, "environmentRotation", &environmentRotation) ||
        !jsonReadOptionalBoolField(settingsObject, "misNeeEnabled", &misNeeEnabled)) {
        return 0;
    }

    (void)savedToLoadedMeshIndex;
    (void)savedMeshCount;

    return VKRT_setPathDepth(vkrt, rrMinDepth, rrMaxDepth) == VKRT_SUCCESS &&
           VKRT_setToneMappingMode(vkrt, toneMappingMode) == VKRT_SUCCESS &&
           VKRT_setRenderMode(vkrt, (VKRT_RenderMode)renderMode) == VKRT_SUCCESS &&
           VKRT_setSpectralSamplingMode(vkrt, spectralSamplingMode) == VKRT_SUCCESS &&
           VKRT_setExposure(vkrt, exposure) == VKRT_SUCCESS &&
           VKRT_setAutoExposureEnabled(vkrt, autoExposureEnabled) == VKRT_SUCCESS &&
           VKRT_setEnvironmentLight(vkrt, environmentColor, environmentStrength) == VKRT_SUCCESS &&
           VKRT_setEnvironmentRotation(vkrt, environmentRotation) == VKRT_SUCCESS &&
           VKRT_setMISNEEEnabled(vkrt, misNeeEnabled ? 1u : 0u) == VKRT_SUCCESS &&
           VKRT_cameraSetPose(vkrt, cameraPosition, cameraTarget, cameraUp, vfov) == VKRT_SUCCESS;
}

static int queryHighestSavedMaterialIndex(
    const cJSON* materialsArray,
    const cJSON* meshesArray,
    uint32_t* outHighestIndex
) {
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

static int createSceneRoot(
    cJSON** outRoot,
    cJSON** outMeshImportsArray,
    cJSON** outTextureImportsArray,
    cJSON** outMaterialsArray,
    cJSON** outMeshesArray,
    cJSON** outSceneObjectsArray
) {
    if (!outRoot || !outMeshImportsArray || !outTextureImportsArray || !outMaterialsArray || !outMeshesArray ||
        !outSceneObjectsArray) {
        return 0;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root) return 0;

    cJSON* meshImportsArray = cJSON_CreateArray();
    cJSON* textureImportsArray = cJSON_CreateArray();
    cJSON* materialsArray = cJSON_CreateArray();
    cJSON* meshesArray = cJSON_CreateArray();
    cJSON* sceneObjectsArray = cJSON_CreateArray();
    if (!meshImportsArray || !textureImportsArray || !materialsArray || !meshesArray || !sceneObjectsArray) {
        cJSON_Delete(meshImportsArray);
        cJSON_Delete(textureImportsArray);
        cJSON_Delete(materialsArray);
        cJSON_Delete(meshesArray);
        cJSON_Delete(sceneObjectsArray);
        cJSON_Delete(root);
        return 0;
    }

    cJSON_AddStringToObject(root, "format", "vkrt.scene");
    cJSON_AddNumberToObject(root, "version", K_SCENE_FILE_VERSION);
    cJSON_AddItemToObject(root, "meshImports", meshImportsArray);
    cJSON_AddItemToObject(root, "textureImports", textureImportsArray);
    cJSON_AddItemToObject(root, "materials", materialsArray);
    cJSON_AddItemToObject(root, "meshes", meshesArray);
    cJSON_AddItemToObject(root, "sceneObjects", sceneObjectsArray);

    *outRoot = root;
    *outMeshImportsArray = meshImportsArray;
    *outTextureImportsArray = textureImportsArray;
    *outMaterialsArray = materialsArray;
    *outMeshesArray = meshesArray;
    *outSceneObjectsArray = sceneObjectsArray;
    return 1;
}

static int addEnvironmentTexturePathJSON(cJSON* root, Session* session, const char* scenePath) {
    if (!root || !session) return 0;
    if (!sessionGetEnvironmentTexturePath(session)[0]) return 1;

    char storedPath[VKRT_PATH_MAX];
    if (!copyPortableStoredPath(scenePath, sessionGetEnvironmentTexturePath(session), storedPath)) return 0;
    cJSON_AddStringToObject(root, "environmentTexturePath", storedPath);
    return 1;
}

static int appendMaterialsJSON(cJSON* materialsArray, VKRT* vkrt, uint32_t materialCount) {
    if (!materialsArray || !vkrt) return 0;

    for (uint32_t materialIndex = 1u; materialIndex < materialCount; materialIndex++) {
        VKRT_MaterialSnapshot material = {0};
        if (VKRT_getMaterialSnapshot(vkrt, materialIndex, &material) != VKRT_SUCCESS) return 0;

        cJSON* materialObject = cJSON_CreateObject();
        cJSON* materialJSON = createMaterialJSON(&material.material);
        if (!materialObject || !materialJSON) {
            cJSON_Delete(materialObject);
            cJSON_Delete(materialJSON);
            return 0;
        }

        cJSON_AddNumberToObject(materialObject, "index", materialIndex);
        cJSON_AddStringToObject(materialObject, "name", material.name);
        cJSON_AddItemToObject(materialObject, "material", materialJSON);
        cJSON_AddItemToArray(materialsArray, materialObject);
    }

    return 1;
}

static uint32_t findImportBatchIndex(
    const uint32_t* importBatchIndices,
    uint32_t importBatchCount,
    uint32_t importBatchIndex
) {
    if (!importBatchIndices) return VKRT_INVALID_INDEX;

    for (uint32_t candidateIndex = 0u; candidateIndex < importBatchCount; candidateIndex++) {
        if (importBatchIndices[candidateIndex] == importBatchIndex) return candidateIndex;
    }

    return VKRT_INVALID_INDEX;
}

static int appendMeshImportsAndMeshesJSON(
    cJSON* meshImportsArray,
    cJSON* meshesArray,
    VKRT* vkrt,
    Session* session,
    const char* scenePath,
    uint32_t meshCount
) {
    if (!meshImportsArray || !meshesArray || !vkrt || !session) return 0;

    uint32_t* importBatchIndices =
        meshCount > 0u ? (uint32_t*)malloc((size_t)meshCount * sizeof(*importBatchIndices)) : NULL;
    uint32_t importBatchCount = 0u;
    if (meshCount > 0u && !importBatchIndices) return 0;

    for (uint32_t meshIndex = 0u; meshIndex < meshCount; meshIndex++) {
        const SessionMeshRecord* meshRecord = sessionGetMeshRecord(session, meshIndex);
        VKRT_MeshSnapshot mesh = {0};
        if (!meshRecord || VKRT_getMeshSnapshot(vkrt, meshIndex, &mesh) != VKRT_SUCCESS) {
            free(importBatchIndices);
            return 0;
        }

        uint32_t importIndex = findImportBatchIndex(importBatchIndices, importBatchCount, meshRecord->importBatchIndex);
        if (importIndex == VKRT_INVALID_INDEX) {
            const char* batchPath = sessionGetMeshImportPath(session, meshRecord->importBatchIndex);
            char storedPath[VKRT_PATH_MAX];
            if (!batchPath || !batchPath[0] || !copyPortableStoredPath(scenePath, batchPath, storedPath)) {
                free(importBatchIndices);
                return 0;
            }

            importIndex = importBatchCount;
            importBatchIndices[importBatchCount++] = meshRecord->importBatchIndex;
            cJSON_AddItemToArray(meshImportsArray, cJSON_CreateString(storedPath));
        }

        cJSON* meshObject = cJSON_CreateObject();
        if (!meshObject) {
            free(importBatchIndices);
            return 0;
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
    return 1;
}

static int appendTextureImportsJSON(cJSON* textureImportsArray, Session* session, const char* scenePath) {
    if (!textureImportsArray || !session) return 0;

    for (uint32_t textureIndex = 0u; textureIndex < sessionGetTextureRecordCount(session); textureIndex++) {
        const SessionTextureRecord* texture = sessionGetTextureRecord(session, textureIndex);
        if (!texture || !texture->sourcePath || !texture->sourcePath[0]) continue;

        char storedPath[VKRT_PATH_MAX];
        if (!copyPortableStoredPath(scenePath, texture->sourcePath, storedPath)) return 0;

        cJSON* textureObject = cJSON_CreateObject();
        if (!textureObject) return 0;

        cJSON_AddNumberToObject(textureObject, "index", textureIndex);
        cJSON_AddNumberToObject(textureObject, "colorSpace", texture->colorSpace);
        cJSON_AddStringToObject(textureObject, "path", storedPath);
        cJSON_AddItemToArray(textureImportsArray, textureObject);
    }

    return 1;
}

static int appendSceneObjectsJSON(cJSON* sceneObjectsArray, Session* session) {
    if (!sceneObjectsArray || !session) return 0;

    for (uint32_t objectIndex = 0u; objectIndex < sessionGetSceneObjectCount(session); objectIndex++) {
        const SessionSceneObject* object = sessionGetSceneObject(session, objectIndex);
        if (!object) continue;

        cJSON* objectJSON = cJSON_CreateObject();
        if (!objectJSON) return 0;

        cJSON_AddStringToObject(objectJSON, "name", object->name);
        addUIntOrNull(objectJSON, "parentIndex", object->parentIndex);
        addUIntOrNull(objectJSON, "meshIndex", object->meshIndex);
        if (!addSceneObjectTransformJSON(objectJSON, object)) {
            cJSON_Delete(objectJSON);
            return 0;
        }
        cJSON_AddItemToArray(sceneObjectsArray, objectJSON);
    }

    return 1;
}

static cJSON* createSceneJSON(VKRT* vkrt, Session* session, const char* scenePath) {
    if (!vkrt || !session) return NULL;

    VKRT_SceneSettingsSnapshot settings = {0};
    uint32_t meshCount = 0u;
    uint32_t materialCount = 0u;
    if (VKRT_getSceneSettings(vkrt, &settings) != VKRT_SUCCESS || VKRT_getMeshCount(vkrt, &meshCount) != VKRT_SUCCESS ||
        VKRT_getMaterialCount(vkrt, &materialCount) != VKRT_SUCCESS ||
        sessionGetMeshRecordCount(session) != meshCount) {
        return NULL;
    }

    cJSON* root = NULL;
    cJSON* meshImportsArray = NULL;
    cJSON* textureImportsArray = NULL;
    cJSON* materialsArray = NULL;
    cJSON* meshesArray = NULL;
    cJSON* sceneObjectsArray = NULL;
    if (!createSceneRoot(
            &root,
            &meshImportsArray,
            &textureImportsArray,
            &materialsArray,
            &meshesArray,
            &sceneObjectsArray
        ) ||
        !saveSceneSettings(root, &settings) || !addEnvironmentTexturePathJSON(root, session, scenePath) ||
        !appendMaterialsJSON(materialsArray, vkrt, materialCount) ||
        !appendMeshImportsAndMeshesJSON(meshImportsArray, meshesArray, vkrt, session, scenePath, meshCount) ||
        !appendTextureImportsJSON(textureImportsArray, session, scenePath) ||
        !appendSceneObjectsJSON(sceneObjectsArray, session)) {
        cJSON_Delete(root);
        return NULL;
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
    (void)fclose(file);
    cJSON_free(jsonText);
    if (written != length) return 0;

    sessionSetCurrentScenePath(session, path);
    return 1;
}

static void cleanupSceneLoadContext(SceneLoadContext* context) {
    if (!context) return;
    free(context->textureIndexMap);
    free(context->savedToLoadedMeshIndex);
    free(context->loadedBatches);
    free(context->keepMesh);
    cJSON_Delete(context->root);
}

static int allocateSceneLoadMaps(SceneLoadContext* context, uint32_t savedMeshCount, uint32_t textureIndexMapCount) {
    if (!context) return 0;

    context->textureIndexMap =
        textureIndexMapCount > 0u ? (uint32_t*)malloc((size_t)textureIndexMapCount * sizeof(uint32_t)) : NULL;
    context->savedToLoadedMeshIndex =
        savedMeshCount > 0u ? (uint32_t*)malloc((size_t)savedMeshCount * sizeof(uint32_t)) : NULL;

    return (textureIndexMapCount == 0u || context->textureIndexMap) &&
           (savedMeshCount == 0u || context->savedToLoadedMeshIndex);
}

static void clearSceneLoadMaps(SceneLoadContext* context, uint32_t savedMeshCount, uint32_t textureIndexMapCount) {
    if (!context) return;

    for (uint32_t textureIndex = 0u; textureIndex < textureIndexMapCount; textureIndex++) {
        context->textureIndexMap[textureIndex] = VKRT_INVALID_INDEX;
    }
    for (uint32_t meshIndex = 0u; meshIndex < savedMeshCount; meshIndex++) {
        context->savedToLoadedMeshIndex[meshIndex] = VKRT_INVALID_INDEX;
    }
}

static int loadImportedMeshBatches(SceneLoadContext* context, const cJSON* meshImportsArray) {
    if (!context || !meshImportsArray) return 0;

    cJSON* meshImport = NULL;
    cJSON_ArrayForEach(meshImport, (cJSON*)meshImportsArray) {
        char resolvedPath[VKRT_PATH_MAX];
        if (!meshImport || !cJSON_IsString(meshImport) ||
            !resolveSceneAssetPath(context->path, meshImport->valuestring, resolvedPath)) {
            return 0;
        }

        uint32_t meshCountBefore = 0u;
        uint32_t meshCountAfter = 0u;
        if (VKRT_getMeshCount(context->vkrt, &meshCountBefore) != VKRT_SUCCESS ||
            !meshControllerImportMesh(context->vkrt, context->session, resolvedPath, NULL, NULL) ||
            VKRT_getMeshCount(context->vkrt, &meshCountAfter) != VKRT_SUCCESS || meshCountAfter < meshCountBefore) {
            return 0;
        }

        LoadedMeshImportBatch* resizedBatches = (LoadedMeshImportBatch*)
            realloc(context->loadedBatches, (size_t)(context->loadedBatchCount + 1u) * sizeof(*context->loadedBatches));
        if (!resizedBatches) return 0;

        context->loadedBatches = resizedBatches;
        context->loadedBatches[context->loadedBatchCount++] = (LoadedMeshImportBatch){
            .firstMeshIndex = meshCountBefore,
            .meshCount = meshCountAfter - meshCountBefore,
        };
    }

    return 1;
}

static int loadStandaloneTextures(SceneLoadContext* context, const cJSON* textureImportsArray) {
    if (!context) return 0;
    if (!textureImportsArray) return 1;

    cJSON* textureObject = NULL;
    cJSON_ArrayForEach(textureObject, (cJSON*)textureImportsArray) {
        const cJSON* pathItem = cJSON_GetObjectItemCaseSensitive(textureObject, "path");
        uint32_t savedTextureIndex = 0u;
        uint32_t colorSpace = 0u;
        char resolvedPath[VKRT_PATH_MAX];
        uint32_t newTextureIndex = VKRT_INVALID_INDEX;
        if (!textureObject || !cJSON_IsObject(textureObject) || !pathItem || !cJSON_IsString(pathItem) ||
            !jsonToUInt32(cJSON_GetObjectItemCaseSensitive(textureObject, "index"), &savedTextureIndex) ||
            !jsonToUInt32(cJSON_GetObjectItemCaseSensitive(textureObject, "colorSpace"), &colorSpace) ||
            !resolveSceneAssetPath(context->path, pathItem->valuestring, resolvedPath) ||
            VKRT_addTextureFromFile(context->vkrt, resolvedPath, NULL, colorSpace, &newTextureIndex) != VKRT_SUCCESS ||
            !sessionAppendStandaloneTextureRecord(context->session, resolvedPath, colorSpace) ||
            savedTextureIndex >= context->textureIndexMapCount) {
            return 0;
        }
        context->textureIndexMap[savedTextureIndex] = newTextureIndex;
    }

    return 1;
}

static int loadEnvironmentTexture(SceneLoadContext* context, const cJSON* environmentTexturePath) {
    if (!context) return 0;
    if (!environmentTexturePath || !cJSON_IsString(environmentTexturePath)) return 1;

    char resolvedEnvironmentPath[VKRT_PATH_MAX];
    if (!resolveSceneAssetPath(context->path, environmentTexturePath->valuestring, resolvedEnvironmentPath) ||
        VKRT_setEnvironmentTextureFromFile(context->vkrt, resolvedEnvironmentPath) != VKRT_SUCCESS) {
        return 0;
    }

    sessionSetEnvironmentTexturePath(context->session, resolvedEnvironmentPath);
    return 1;
}

static int buildSavedMeshIndexMap(SceneLoadContext* context) {
    if (!context) return 0;
    if (context->savedMeshCount == 0u) return 1;
    if (!context->savedToLoadedMeshIndex) return 0;

    cJSON* meshObject = NULL;
    uint32_t savedMeshIndex = 0u;
    cJSON_ArrayForEach(meshObject, (cJSON*)context->meshesArray) {
        uint32_t importIndex = 0u;
        uint32_t importLocalIndex = 0u;
        if (savedMeshIndex >= context->savedMeshCount ||
            !jsonToUInt32(cJSON_GetObjectItemCaseSensitive(meshObject, "importIndex"), &importIndex) ||
            !jsonToUInt32(cJSON_GetObjectItemCaseSensitive(meshObject, "importLocalIndex"), &importLocalIndex) ||
            importIndex >= context->loadedBatchCount ||
            importLocalIndex >= context->loadedBatches[importIndex].meshCount) {
            return 0;
        }

        context->savedToLoadedMeshIndex[savedMeshIndex++] =
            context->loadedBatches[importIndex].firstMeshIndex + importLocalIndex;
    }

    return 1;
}

static int pruneNonSceneMeshes(SceneLoadContext* context) {
    if (!context) return 0;

    uint32_t currentMeshCount = 0u;
    if (VKRT_getMeshCount(context->vkrt, &currentMeshCount) != VKRT_SUCCESS) return 0;

    context->keepMesh = currentMeshCount > 0u ? (uint8_t*)calloc(currentMeshCount, sizeof(uint8_t)) : NULL;
    if (currentMeshCount > 0u && !context->keepMesh) return 0;

    for (uint32_t meshIndex = 0u; meshIndex < context->savedMeshCount; meshIndex++) {
        if (!context->savedToLoadedMeshIndex || context->savedToLoadedMeshIndex[meshIndex] >= currentMeshCount) {
            return 0;
        }
        context->keepMesh[context->savedToLoadedMeshIndex[meshIndex]] = 1u;
    }

    sessionTruncateSceneObjects(context->session, 0u);
    sessionSetSelectedSceneObject(context->session, VKRT_INVALID_INDEX);
    for (uint32_t meshIndex = currentMeshCount; meshIndex > 0u; meshIndex--) {
        uint32_t removeIndex = meshIndex - 1u;
        if (context->keepMesh[removeIndex]) continue;
        if (VKRT_removeMesh(context->vkrt, removeIndex) != VKRT_SUCCESS) return 0;

        sessionRemoveMeshRecord(context->session, removeIndex);
        sessionRemoveMeshReferences(context->session, removeIndex);
        for (uint32_t savedMeshIndex = 0u; savedMeshIndex < context->savedMeshCount; savedMeshIndex++) {
            if (context->savedToLoadedMeshIndex[savedMeshIndex] != VKRT_INVALID_INDEX &&
                context->savedToLoadedMeshIndex[savedMeshIndex] > removeIndex) {
                context->savedToLoadedMeshIndex[savedMeshIndex]--;
            }
        }
    }

    return 1;
}

static int applyLoadedMaterials(SceneLoadContext* context) {
    if (!context) return 0;

    uint32_t highestSavedMaterialIndex = 0u;
    if (!queryHighestSavedMaterialIndex(context->materialsArray, context->meshesArray, &highestSavedMaterialIndex) ||
        !ensureMaterialCapacityForSceneLoad(context->vkrt, highestSavedMaterialIndex + 1u)) {
        return 0;
    }

    if (!context->materialsArray) return 1;

    cJSON* materialObject = NULL;
    uint32_t contiguousMaterialIndex = 0u;
    cJSON_ArrayForEach(materialObject, (cJSON*)context->materialsArray) {
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(materialObject, "name");
        const cJSON* materialValue = cJSON_GetObjectItemCaseSensitive(materialObject, "material");
        const cJSON* explicitIndex = cJSON_GetObjectItemCaseSensitive(materialObject, "index");
        uint32_t materialIndex = contiguousMaterialIndex;
        Material material = VKRT_materialDefault();
        if ((explicitIndex && !jsonToUInt32(explicitIndex, &materialIndex)) || !name || !cJSON_IsString(name) ||
            !parseMaterialJSON(materialValue, &material)) {
            return 0;
        }

        remapStandaloneTextureIndices(&material, context->textureIndexMap, context->textureIndexMapCount);
        if (VKRT_setMaterial(context->vkrt, materialIndex, &material) != VKRT_SUCCESS ||
            VKRT_setMaterialName(context->vkrt, materialIndex, name->valuestring) != VKRT_SUCCESS) {
            return 0;
        }
        contiguousMaterialIndex++;
    }

    return 1;
}

static int applyLoadedMeshes(SceneLoadContext* context) {
    if (!context) return 0;
    if (context->savedMeshCount > 0u && !context->savedToLoadedMeshIndex) return 0;

    cJSON* meshObject = NULL;
    uint32_t savedMeshIndex = 0u;
    cJSON_ArrayForEach(meshObject, (cJSON*)context->meshesArray) {
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(meshObject, "name");
        const cJSON* hasMaterialAssignment = cJSON_GetObjectItemCaseSensitive(meshObject, "hasMaterialAssignment");
        const cJSON* renderBackfaces = cJSON_GetObjectItemCaseSensitive(meshObject, "renderBackfaces");
        uint32_t materialRef = 0u;
        uint8_t assigned = 0u;
        uint8_t backfaces = 0u;
        float opacity = 1.0f;
        if (savedMeshIndex >= context->savedMeshCount) return 0;

        uint32_t loadedMeshIndex = context->savedToLoadedMeshIndex[savedMeshIndex++];
        if (!name || !cJSON_IsString(name) ||
            !jsonToUInt32(cJSON_GetObjectItemCaseSensitive(meshObject, "materialIndex"), &materialRef) ||
            !jsonToBool(hasMaterialAssignment, &assigned) || !jsonToBool(renderBackfaces, &backfaces) ||
            !jsonToFloat(cJSON_GetObjectItemCaseSensitive(meshObject, "opacity"), &opacity) ||
            VKRT_setMeshName(context->vkrt, loadedMeshIndex, name->valuestring) != VKRT_SUCCESS ||
            (assigned ? VKRT_setMeshMaterialIndex(context->vkrt, loadedMeshIndex, materialRef)
                      : VKRT_clearMeshMaterialAssignment(context->vkrt, loadedMeshIndex)) != VKRT_SUCCESS ||
            VKRT_setMeshOpacity(context->vkrt, loadedMeshIndex, opacity) != VKRT_SUCCESS ||
            VKRT_setMeshRenderBackfaces(context->vkrt, loadedMeshIndex, backfaces ? 1u : 0u) != VKRT_SUCCESS) {
            return 0;
        }
    }

    return 1;
}

static int resolveLoadedSceneObjectMeshIndex(
    const SceneLoadContext* context,
    uint32_t savedMeshRef,
    uint32_t* outLoadedMeshIndex
) {
    if (outLoadedMeshIndex) *outLoadedMeshIndex = VKRT_INVALID_INDEX;
    if (!context || !outLoadedMeshIndex) return 0;
    if (savedMeshRef == VKRT_INVALID_INDEX) return 1;
    if (savedMeshRef >= context->savedMeshCount || !context->savedToLoadedMeshIndex) return 0;

    *outLoadedMeshIndex = context->savedToLoadedMeshIndex[savedMeshRef];
    return 1;
}

static int applyLoadedSceneObjects(SceneLoadContext* context) {
    if (!context) return 0;

    cJSON* sceneObject = NULL;
    uint32_t sceneObjectIndex = 0u;
    cJSON_ArrayForEach(sceneObject, (cJSON*)context->sceneObjectsArray) {
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(sceneObject, "name");
        uint32_t parentIndex = VKRT_INVALID_INDEX;
        uint32_t savedMeshRef = VKRT_INVALID_INDEX;
        vec3 localPosition = {0.0f, 0.0f, 0.0f};
        vec3 localRotation = {0.0f, 0.0f, 0.0f};
        vec3 localScale = {1.0f, 1.0f, 1.0f};
        mat4 localTransform = {
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 0.0f, 1.0f},
        };
        uint8_t useMatrix = 0u;
        const vec3 zero = {0.0f, 0.0f, 0.0f};
        const vec3 one = {1.0f, 1.0f, 1.0f};
        uint32_t loadedMeshIndex = VKRT_INVALID_INDEX;
        if (!name || !cJSON_IsString(name) ||
            !jsonToIndexOrInvalid(cJSON_GetObjectItemCaseSensitive(sceneObject, "parentIndex"), &parentIndex) ||
            !jsonToIndexOrInvalid(cJSON_GetObjectItemCaseSensitive(sceneObject, "meshIndex"), &savedMeshRef) ||
            !parseSceneObjectTransformJSON(
                sceneObject,
                localPosition,
                localRotation,
                localScale,
                localTransform,
                &useMatrix
            )) {
            return 0;
        }
        if (parentIndex != VKRT_INVALID_INDEX && parentIndex >= sceneObjectIndex) return 0;

        if (!resolveLoadedSceneObjectMeshIndex(context, savedMeshRef, &loadedMeshIndex)) return 0;

        const vec3 createPosition = {localPosition[0], localPosition[1], localPosition[2]};
        const vec3 createRotation = {localRotation[0], localRotation[1], localRotation[2]};
        const vec3 createScale = {localScale[0], localScale[1], localScale[2]};

        if (!sessionAddSceneObject(
                context->session,
                &(SessionSceneObjectCreateInfo){
                    .name = name->valuestring,
                    .parentIndex = parentIndex,
                    .meshIndex = loadedMeshIndex,
                    .localPosition = useMatrix ? &zero : &createPosition,
                    .localRotation = useMatrix ? &zero : &createRotation,
                    .localScale = useMatrix ? &one : &createScale,
                },
                NULL
            ) ||
            (useMatrix &&
             !sessionSetSceneObjectLocalTransformMatrix(context->session, sceneObjectIndex, localTransform))) {
            return 0;
        }

        sceneObjectIndex++;
    }

    return sessionSyncSceneObjectTransforms(context->vkrt, context->session) && applySceneSettings(
                                                                                    context->vkrt,
                                                                                    context->settingsObject,
                                                                                    context->savedToLoadedMeshIndex,
                                                                                    context->savedMeshCount
                                                                                );
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
        !jsonToUInt32(version, &fileVersion) || fileVersion != K_SCENE_FILE_VERSION ||
        !cJSON_IsArray(meshImportsArray) || (textureImportsArray && !cJSON_IsArray(textureImportsArray)) ||
        !cJSON_IsArray(meshesArray) || !cJSON_IsArray(sceneObjectsArray) || !cJSON_IsObject(settingsObject) ||
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
    SceneLoadContext context = {
        .vkrt = vkrt,
        .session = session,
        .root = root,
        .materialsArray = materialsArray,
        .meshesArray = meshesArray,
        .sceneObjectsArray = sceneObjectsArray,
        .settingsObject = settingsObject,
        .path = path,
        .targetScenePath = targetScenePath,
        .savedMeshCount = savedMeshCount,
        .textureIndexMapCount = textureIndexMapCount,
    };
    if (!allocateSceneLoadMaps(&context, savedMeshCount, textureIndexMapCount)) {
        cleanupSceneLoadContext(&context);
        return 0;
    }
    clearSceneLoadMaps(&context, savedMeshCount, textureIndexMapCount);

    sessionSetCurrentScenePath(session, NULL);
    if (!clearCurrentScene(vkrt, session)) {
        cleanupSceneLoadContext(&context);
        return 0;
    }

    if (!loadImportedMeshBatches(&context, meshImportsArray) ||
        !loadStandaloneTextures(&context, textureImportsArray) ||
        !loadEnvironmentTexture(&context, environmentTexturePath) || !buildSavedMeshIndexMap(&context) ||
        !pruneNonSceneMeshes(&context) || !applyLoadedMaterials(&context) || !applyLoadedMeshes(&context) ||
        !applyLoadedSceneObjects(&context)) {
        cleanupSceneLoadContext(&context);
        return 0;
    }

    sessionSetCurrentScenePath(session, targetScenePath && targetScenePath[0] ? targetScenePath : NULL);
    cleanupSceneLoadContext(&context);
    return 1;
}

int sceneControllerLoadSceneFromPath(VKRT* vkrt, Session* session, const char* path) {
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
    int haveSceneState =
        querySceneHasBackupState(vkrt, session, previousScenePath, &meshCount, &materialCount, &textureCount);

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
    return sceneControllerLoadSceneFromPath(vkrt, session, resolvedPath);
}

int sceneControllerLoadStartupScene(
    VKRT* vkrt,
    Session* session,
    const char* startupScenePath,
    uint8_t loadDefaultScene
) {
    if (!vkrt || !session) return 0;
    if (startupScenePath) return sceneControllerLoadSceneFromPath(vkrt, session, startupScenePath);
    if (loadDefaultScene) return sceneControllerLoadDefaultScene(vkrt, session);
    return 1;
}

void sceneControllerApplySessionActions(VKRT* vkrt, Session* session) {
    if (!vkrt || !session) return;

    char* openScenePath = NULL;
    if (sessionTakeSceneOpen(session, &openScenePath)) {
        if (!sceneControllerLoadSceneFromPath(vkrt, session, openScenePath)) {
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
