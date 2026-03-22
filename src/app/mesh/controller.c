#include "controller.h"
#include "loader.h"
#include "debug.h"
#include "vkrt.h"
#include "vkrt_types.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct DefaultMeshSpec {
    const char* assetPath;
    const char* importName;
    vec3 position;
    vec3 rotation;
    vec3 scale;
    Material material;
} DefaultMeshSpec;

static uint32_t colorSpaceForTextureSlot(uint32_t textureSlot) {
    switch (textureSlot) {
        case VKRT_MATERIAL_TEXTURE_SLOT_BASE_COLOR:
        case VKRT_MATERIAL_TEXTURE_SLOT_EMISSIVE:
            return VKRT_TEXTURE_COLOR_SPACE_SRGB;
        case VKRT_MATERIAL_TEXTURE_SLOT_METALLIC_ROUGHNESS:
        case VKRT_MATERIAL_TEXTURE_SLOT_NORMAL:
        default:
            return VKRT_TEXTURE_COLOR_SPACE_LINEAR;
    }
}

static void rollbackImportedMeshes(VKRT* vkrt, uint32_t meshCountBefore) {
    if (!vkrt) return;

    uint32_t meshCount = 0;
    if (VKRT_getMeshCount(vkrt, &meshCount) != VKRT_SUCCESS) return;

    while (meshCount > meshCountBefore) {
        uint32_t meshIndex = meshCount - 1u;
        if (VKRT_removeMesh(vkrt, meshIndex) != VKRT_SUCCESS) {
            LOG_ERROR("Rolling back imported mesh failed at index %u", meshIndex);
            break;
        }
        meshCount--;
    }
}

static void rollbackImportedMaterials(VKRT* vkrt, uint32_t materialCountBefore) {
    if (!vkrt) return;

    uint32_t materialCount = 0;
    if (VKRT_getMaterialCount(vkrt, &materialCount) != VKRT_SUCCESS) return;
    uint32_t targetCount = materialCountBefore > 0u ? materialCountBefore : 1u;

    while (materialCount > targetCount) {
        uint32_t materialIndex = materialCount - 1u;
        if (VKRT_removeMaterial(vkrt, materialIndex) != VKRT_SUCCESS) {
            LOG_ERROR("Rolling back imported material failed at index %u", materialIndex);
            break;
        }
        materialCount--;
    }
}

static void rollbackImportedTextures(VKRT* vkrt, uint32_t textureCountBefore) {
    if (!vkrt) return;

    uint32_t textureCount = 0;
    if (VKRT_getTextureCount(vkrt, &textureCount) != VKRT_SUCCESS) return;

    while (textureCount > textureCountBefore) {
        uint32_t textureIndex = textureCount - 1u;
        if (VKRT_removeTexture(vkrt, textureIndex) != VKRT_SUCCESS) {
            LOG_ERROR("Rolling back imported texture failed at index %u", textureIndex);
            break;
        }
        textureCount--;
    }
}

static void rollbackImportedSceneObjects(Session* session, uint32_t objectCountBeforeImport) {
    if (!session) return;
    sessionTruncateSceneObjects(session, objectCountBeforeImport);
}

static int sceneObjectBelongsToSubtree(const Session* session, uint32_t objectIndex, uint32_t rootObjectIndex) {
    if (!session || objectIndex >= sessionGetSceneObjectCount(session)) return 0;

    uint32_t currentIndex = objectIndex;
    while (currentIndex != VKRT_INVALID_INDEX) {
        if (currentIndex == rootObjectIndex) return 1;
        const SessionSceneObject* object = sessionGetSceneObject(session, currentIndex);
        if (!object) break;
        currentIndex = object->parentIndex;
    }
    return 0;
}

static uint32_t collectSceneObjectSubtreeMeshIndices(
    const Session* session,
    uint32_t rootObjectIndex,
    uint32_t* outMeshIndices,
    uint32_t capacity
) {
    if (!session || !outMeshIndices || capacity == 0u) return 0u;

    uint32_t count = 0u;
    uint32_t objectCount = sessionGetSceneObjectCount(session);
    for (uint32_t objectIndex = 0; objectIndex < objectCount; objectIndex++) {
        if (!sceneObjectBelongsToSubtree(session, objectIndex, rootObjectIndex)) continue;

        const SessionSceneObject* object = sessionGetSceneObject(session, objectIndex);
        if (!object || object->meshIndex == VKRT_INVALID_INDEX) continue;
        if (count >= capacity) break;
        outMeshIndices[count++] = object->meshIndex;
    }

    return count;
}

static void sortMeshIndicesDescending(uint32_t* meshIndices, uint32_t count) {
    if (!meshIndices || count < 2u) return;

    for (uint32_t i = 1u; i < count; i++) {
        uint32_t value = meshIndices[i];
        uint32_t j = i;
        while (j > 0u && meshIndices[j - 1u] < value) {
            meshIndices[j] = meshIndices[j - 1u];
            j--;
        }
        meshIndices[j] = value;
    }
}

static int removeSceneObjectHierarchy(VKRT* vkrt, Session* session, uint32_t objectIndex) {
    if (!vkrt || !session || objectIndex >= sessionGetSceneObjectCount(session)) return 0;

    uint32_t objectCount = sessionGetSceneObjectCount(session);
    uint32_t* meshIndices = objectCount > 0u
        ? (uint32_t*)malloc((size_t)objectCount * sizeof(uint32_t))
        : NULL;
    if (objectCount > 0u && !meshIndices) {
        return 0;
    }

    uint32_t meshCount = collectSceneObjectSubtreeMeshIndices(session, objectIndex, meshIndices, objectCount);
    sortMeshIndicesDescending(meshIndices, meshCount);

    for (uint32_t i = 0; i < meshCount; i++) {
        VKRT_Result result = VKRT_removeMesh(vkrt, meshIndices[i]);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Removing scene object mesh failed (%d)", (int)result);
            free(meshIndices);
            return 0;
        }
        sessionRemoveMeshReferencesNoPrune(session, meshIndices[i]);
    }

    sessionRemoveSceneObjectSubtree(session, objectIndex);

    free(meshIndices);
    return 1;
}

static void remapImportedMaterialTextures(Material* material, const uint32_t* textureIndexMap, uint32_t textureCount) {
    if (!material || !textureIndexMap) return;

    uint32_t* slots[] = {
        &material->baseColorTextureIndex,
        &material->metallicRoughnessTextureIndex,
        &material->normalTextureIndex,
        &material->emissiveTextureIndex,
    };

    for (uint32_t i = 0; i < VKRT_ARRAY_COUNT(slots); i++) {
        if (*slots[i] == VKRT_INVALID_INDEX) {
            continue;
        }
        if (*slots[i] >= textureCount) {
            *slots[i] = VKRT_INVALID_INDEX;
            continue;
        }
        *slots[i] = textureIndexMap[*slots[i]];
    }
}

static void applyMeshMaterial(VKRT* vkrt, uint32_t meshIndex, const DefaultMeshSpec* spec) {
    if (!vkrt || !spec || meshIndex == VKRT_INVALID_INDEX) return;

    uint32_t materialIndex = VKRT_INVALID_INDEX;
    VKRT_Result result = VKRT_addMaterial(vkrt, &spec->material, spec->importName, &materialIndex);
    if (result == VKRT_SUCCESS) {
        result = VKRT_setMeshMaterialIndex(vkrt, meshIndex, materialIndex);
        if (result != VKRT_SUCCESS) {
            (void)VKRT_removeMaterial(vkrt, materialIndex);
        }
    }
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Setting %s material failed (%d)", spec->importName, (int)result);
    }
}

static void importDefaultMesh(VKRT* vkrt, Session* session, const DefaultMeshSpec* spec) {
    if (!vkrt || !session || !spec) return;

    uint32_t meshIndex = VKRT_INVALID_INDEX;
    if (!meshControllerImportMesh(vkrt, session, spec->assetPath, spec->importName, &meshIndex)) {
        return;
    }

    vec3 position = {spec->position[0], spec->position[1], spec->position[2]};
    vec3 rotation = {spec->rotation[0], spec->rotation[1], spec->rotation[2]};
    vec3 scale = {spec->scale[0], spec->scale[1], spec->scale[2]};
    if (sessionSetSceneObjectLocalTransformForMesh(session, meshIndex, position, rotation, scale) &&
        !sessionSyncSceneObjectTransforms(vkrt, session)) {
        LOG_ERROR("Setting %s transform failed", spec->importName);
    }
    applyMeshMaterial(vkrt, meshIndex, spec);
}

static int buildImportedSceneObjects(
    VKRT* vkrt,
    Session* session,
    const MeshImportData* importData,
    uint32_t meshIndexBase,
    const char* importName
) {
    if (!vkrt || !session || !importData) return 0;

    uint32_t* nodeObjectIndices = importData->nodeCount > 0
        ? (uint32_t*)malloc(importData->nodeCount * sizeof(uint32_t))
        : NULL;
    if (importData->nodeCount > 0 && !nodeObjectIndices) {
        return 0;
    }

    for (uint32_t nodeIndex = 0; nodeIndex < importData->nodeCount; nodeIndex++) {
        const NodeImportEntry* node = &importData->nodes[nodeIndex];
        uint32_t parentObjectIndex = node->parentIndex != VKRT_INVALID_INDEX
            ? nodeObjectIndices[node->parentIndex]
            : VKRT_INVALID_INDEX;
        vec3 position = {node->position[0], node->position[1], node->position[2]};
        vec3 rotation = {node->rotation[0], node->rotation[1], node->rotation[2]};
        vec3 scale = {node->scale[0], node->scale[1], node->scale[2]};
        const char* nodeName = (node->name && node->name[0]) ? node->name : "Object";
        if (!sessionAddSceneObject(
                session,
                nodeName,
                parentObjectIndex,
                VKRT_INVALID_INDEX,
                position,
                rotation,
                scale,
                &nodeObjectIndices[nodeIndex])) {
            free(nodeObjectIndices);
            return 0;
        }
        mat4 nodeLocalTransform = GLM_MAT4_IDENTITY_INIT;
        memcpy(nodeLocalTransform, node->localTransform, sizeof(nodeLocalTransform));
        if (!sessionSetSceneObjectLocalTransformMatrix(session, nodeObjectIndices[nodeIndex], nodeLocalTransform)) {
            free(nodeObjectIndices);
            return 0;
        }
    }

    vec3 zero = {0.0f, 0.0f, 0.0f};
    vec3 one = {1.0f, 1.0f, 1.0f};
    for (uint32_t entryIndex = 0; entryIndex < importData->count; entryIndex++) {
        const MeshImportEntry* entry = &importData->entries[entryIndex];
        uint32_t meshIndex = meshIndexBase + entryIndex;
        uint32_t nodeIndex = entry->nodeIndex;
        if (nodeIndex != VKRT_INVALID_INDEX &&
            nodeIndex < importData->nodeCount &&
            importData->nodes[nodeIndex].meshEntryCount == 1u) {
            if (!sessionSetSceneObjectMesh(session, nodeObjectIndices[nodeIndex], meshIndex)) {
                free(nodeObjectIndices);
                return 0;
            }
            if (importData->count == 1u && importName && importName[0]) {
                sessionSetSceneObjectName(session, nodeObjectIndices[nodeIndex], importName);
            } else if ((!importData->nodes[nodeIndex].name || !importData->nodes[nodeIndex].name[0]) &&
                       entry->name && entry->name[0]) {
                sessionSetSceneObjectName(session, nodeObjectIndices[nodeIndex], entry->name);
            }
            continue;
        }

        uint32_t parentObjectIndex = (nodeIndex != VKRT_INVALID_INDEX && nodeIndex < importData->nodeCount)
            ? nodeObjectIndices[nodeIndex]
            : VKRT_INVALID_INDEX;
        const char* objectName = (importData->count == 1u && importName && importName[0])
            ? importName
            : (entry->name && entry->name[0] ? entry->name : "Mesh");
        if (!sessionAddSceneObject(
                session,
                objectName,
                parentObjectIndex,
                meshIndex,
                zero,
                zero,
                one,
                NULL)) {
            free(nodeObjectIndices);
            return 0;
        }
    }

    free(nodeObjectIndices);
    return sessionSyncSceneObjectTransforms(vkrt, session);
}

int meshControllerImportMesh(VKRT* vkrt, Session* session, const char* path, const char* importName, uint32_t* outMeshIndex) {
    if (outMeshIndex) *outMeshIndex = VKRT_INVALID_INDEX;
    if (!vkrt || !session || !path || !path[0]) return 0;

    uint64_t startTime = getMicroseconds();
    uint32_t meshCountBeforeImport = 0;
    uint32_t materialCountBeforeImport = 0;
    uint32_t textureCountBeforeImport = 0;
    uint32_t objectCountBeforeImport = sessionGetSceneObjectCount(session);
    if (VKRT_getMeshCount(vkrt, &meshCountBeforeImport) != VKRT_SUCCESS) return 0;
    if (VKRT_getMaterialCount(vkrt, &materialCountBeforeImport) != VKRT_SUCCESS) return 0;
    if (VKRT_getTextureCount(vkrt, &textureCountBeforeImport) != VKRT_SUCCESS) return 0;

    MeshImportData importData = {0};
    if (meshLoadFromFile(path, &importData) != 0) {
        LOG_ERROR("Mesh import failed. File: %s", path);
        return 0;
    }

    uint32_t* importedTextureIndices = NULL;
    if (importData.textureCount > 0) {
        importedTextureIndices = (uint32_t*)malloc(importData.textureCount * sizeof(uint32_t));
        if (!importedTextureIndices) {
            meshReleaseImportData(&importData);
            return 0;
        }
        for (uint32_t i = 0; i < importData.textureCount; i++) {
            importedTextureIndices[i] = VKRT_INVALID_INDEX;
        }
    }

    uint32_t* importedMaterialIndices = NULL;
    if (importData.materialCount > 0) {
        importedMaterialIndices = (uint32_t*)malloc(importData.materialCount * sizeof(uint32_t));
        if (!importedMaterialIndices) {
            free(importedTextureIndices);
            meshReleaseImportData(&importData);
            return 0;
        }
        for (uint32_t i = 0; i < importData.materialCount; i++) {
            importedMaterialIndices[i] = VKRT_INVALID_INDEX;
        }
    }

    uint64_t uploadStartTime = getMicroseconds();
    if (importData.count > 0) {
        VKRT_MeshUpload* uploads = (VKRT_MeshUpload*)calloc(importData.count, sizeof(VKRT_MeshUpload));
        if (!uploads) {
            free(importedTextureIndices);
            free(importedMaterialIndices);
            meshReleaseImportData(&importData);
            return 0;
        }

        for (uint32_t i = 0; i < importData.count; i++) {
            const MeshImportEntry* entry = &importData.entries[i];
            uploads[i].vertices = entry->vertices;
            uploads[i].vertexCount = entry->vertexCount;
            uploads[i].indices = entry->indices;
            uploads[i].indexCount = entry->indexCount;
        }

        if (VKRT_uploadMeshDataBatch(vkrt, uploads, importData.count) != VKRT_SUCCESS) {
            free(uploads);
            LOG_ERROR("Mesh upload failed. File: %s", path);
            rollbackImportedMeshes(vkrt, meshCountBeforeImport);
            free(importedTextureIndices);
            free(importedMaterialIndices);
            meshReleaseImportData(&importData);
            return 0;
        }

        free(uploads);
    }

    LOG_TRACE(
        "Mesh geometry uploaded. File: %s, Meshes: %u, in %.3f ms",
        path,
        importData.count,
        (double)(getMicroseconds() - uploadStartTime) / 1e3
    );

    uint64_t textureUploadStartTime = getMicroseconds();
    if (importData.textureCount > 0) {
        VKRT_TextureUpload* textureUploads = (VKRT_TextureUpload*)calloc(importData.textureCount, sizeof(VKRT_TextureUpload));
        if (!textureUploads) {
            rollbackImportedMeshes(vkrt, meshCountBeforeImport);
            free(importedTextureIndices);
            free(importedMaterialIndices);
            meshReleaseImportData(&importData);
            return 0;
        }

        for (uint32_t textureIndex = 0; textureIndex < importData.textureCount; textureIndex++) {
            const TextureImportEntry* texture = &importData.textures[textureIndex];
            textureUploads[textureIndex] = (VKRT_TextureUpload){
                .name = texture->name,
                .pixels = texture->rgba8,
                .width = texture->width,
                .height = texture->height,
                .colorSpace = texture->colorSpace,
            };
        }

        if (VKRT_addTexturesBatch(vkrt, textureUploads, importData.textureCount, importedTextureIndices) != VKRT_SUCCESS) {
            LOG_ERROR("Texture import failed. File: %s", path);
            free(textureUploads);
            rollbackImportedMeshes(vkrt, meshCountBeforeImport);
            rollbackImportedTextures(vkrt, textureCountBeforeImport);
            free(importedTextureIndices);
            free(importedMaterialIndices);
            meshReleaseImportData(&importData);
            return 0;
        }

        free(textureUploads);
    }

    LOG_TRACE(
        "Mesh textures uploaded. File: %s, Textures: %u, in %.3f ms",
        path,
        importData.textureCount,
        (double)(getMicroseconds() - textureUploadStartTime) / 1e3
    );

    uint64_t materialUploadStartTime = getMicroseconds();
    for (uint32_t materialIndex = 0; materialIndex < importData.materialCount; materialIndex++) {
        const MaterialImportEntry* material = &importData.materials[materialIndex];
        Material remappedMaterial = material->material;
        remapImportedMaterialTextures(&remappedMaterial, importedTextureIndices, importData.textureCount);

        if (VKRT_addMaterial(vkrt, &remappedMaterial, material->name, &importedMaterialIndices[materialIndex]) != VKRT_SUCCESS) {
            LOG_ERROR("Material import failed. File: %s, Material: %s", path, material->name ? material->name : "(unnamed)");
            rollbackImportedMeshes(vkrt, meshCountBeforeImport);
            rollbackImportedMaterials(vkrt, materialCountBeforeImport);
            rollbackImportedTextures(vkrt, textureCountBeforeImport);
            free(importedTextureIndices);
            free(importedMaterialIndices);
            meshReleaseImportData(&importData);
            return 0;
        }
    }

    LOG_TRACE(
        "Mesh materials uploaded. File: %s, Materials: %u, in %.3f ms",
        path,
        importData.materialCount,
        (double)(getMicroseconds() - materialUploadStartTime) / 1e3
    );

    uint64_t configureStartTime = getMicroseconds();
    uint32_t configuredMeshCount = 0;
    for (uint32_t i = 0; i < importData.count; i++) {
        const MeshImportEntry* entry = &importData.entries[i];
        uint32_t meshIndex = meshCountBeforeImport + configuredMeshCount;

        VKRT_Result materialResult = VKRT_SUCCESS;
        if (entry->materialIndex != VKRT_INVALID_INDEX) {
            materialResult = VKRT_setMeshMaterialIndex(vkrt, meshIndex, importedMaterialIndices[entry->materialIndex]);
        }

        if (materialResult != VKRT_SUCCESS ||
            VKRT_setMeshRenderBackfaces(vkrt, meshIndex, entry->renderBackfaces) != VKRT_SUCCESS) {
            LOG_ERROR("Mesh material import failed. File: %s, Entry: %s", path, entry->name ? entry->name : "(unnamed)");
            rollbackImportedMeshes(vkrt, meshCountBeforeImport);
            rollbackImportedMaterials(vkrt, materialCountBeforeImport);
            rollbackImportedTextures(vkrt, textureCountBeforeImport);
            rollbackImportedSceneObjects(session, objectCountBeforeImport);
            free(importedTextureIndices);
            free(importedMaterialIndices);
            meshReleaseImportData(&importData);
            return 0;
        }

        const char* meshName = entry->name;
        if (importName && importName[0] && importData.count == 1) {
            meshName = importName;
        }
        if (!meshName || !meshName[0]) {
            meshName = path;
        }
        if (VKRT_setMeshName(vkrt, meshIndex, meshName) != VKRT_SUCCESS) {
            LOG_ERROR("Mesh imported but failed to store mesh label. File: %s", meshName);
        }

        if (configuredMeshCount == 0 && outMeshIndex) {
            *outMeshIndex = meshIndex;
        }
        configuredMeshCount++;
    }

    if (!buildImportedSceneObjects(vkrt, session, &importData, meshCountBeforeImport, importName)) {
        LOG_ERROR("Scene hierarchy import failed. File: %s", path);
        rollbackImportedSceneObjects(session, objectCountBeforeImport);
        rollbackImportedMeshes(vkrt, meshCountBeforeImport);
        rollbackImportedMaterials(vkrt, materialCountBeforeImport);
        rollbackImportedTextures(vkrt, textureCountBeforeImport);
        free(importedTextureIndices);
        free(importedMaterialIndices);
        meshReleaseImportData(&importData);
        return 0;
    }

    LOG_TRACE(
        "Imported mesh metadata applied. File: %s, Meshes: %u, in %.3f ms",
        path,
        configuredMeshCount,
        (double)(getMicroseconds() - configureStartTime) / 1e3
    );

    free(importedTextureIndices);
    free(importedMaterialIndices);
    meshReleaseImportData(&importData);

    uint32_t meshCountAfterImport = 0;
    if (VKRT_getMeshCount(vkrt, &meshCountAfterImport) != VKRT_SUCCESS || meshCountAfterImport <= meshCountBeforeImport) {
        LOG_ERROR("Mesh import failed. File: %s", path);
        rollbackImportedMeshes(vkrt, meshCountBeforeImport);
        rollbackImportedMaterials(vkrt, materialCountBeforeImport);
        rollbackImportedTextures(vkrt, textureCountBeforeImport);
        return 0;
    }

    LOG_INFO(
        "Mesh import complete. File: %s, Imported Meshes: %u, Total Meshes: %u, in %.3f ms",
        path,
        meshCountAfterImport - meshCountBeforeImport,
        meshCountAfterImport,
        (double)(getMicroseconds() - startTime) / 1e3
    );
    return 1;
}

void meshControllerApplySessionActions(VKRT* vkrt, Session* session) {
    char* importPath = NULL;
    if (sessionTakeMeshImport(session, &importPath)) {
        meshControllerImportMesh(vkrt, session, importPath, NULL, NULL);
        free(importPath);
    }

    char* textureImportPath = NULL;
    uint32_t materialIndex = VKRT_INVALID_INDEX;
    uint32_t textureSlot = VKRT_INVALID_INDEX;
    if (sessionTakeTextureImport(session, &materialIndex, &textureSlot, &textureImportPath)) {
        uint32_t textureIndex = VKRT_INVALID_INDEX;
        VKRT_Result result = VKRT_addTextureFromFile(
            vkrt,
            textureImportPath,
            NULL,
            colorSpaceForTextureSlot(textureSlot),
            &textureIndex
        );
        if (result == VKRT_SUCCESS) {
            result = VKRT_setMaterialTexture(vkrt, materialIndex, textureSlot, textureIndex);
            if (result != VKRT_SUCCESS) {
                (void)VKRT_removeTexture(vkrt, textureIndex);
            }
        }
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Texture import failed (%d). File: %s", (int)result, textureImportPath);
        }
        free(textureImportPath);
    }

    char* environmentImportPath = NULL;
    if (sessionTakeEnvironmentImport(session, &environmentImportPath)) {
        VKRT_Result result = VKRT_setEnvironmentTextureFromFile(vkrt, environmentImportPath);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Environment import failed (%d). File: %s", (int)result, environmentImportPath);
        }
        free(environmentImportPath);
    }

    uint32_t removeObjectIndex = VKRT_INVALID_INDEX;
    if (sessionTakeSceneObjectRemoval(session, &removeObjectIndex)) {
        if (!removeSceneObjectHierarchy(vkrt, session, removeObjectIndex)) {
            LOG_ERROR("Removing scene object hierarchy failed");
        }
    }

    uint32_t removeIndex = VKRT_INVALID_INDEX;
    if (sessionTakeMeshRemoval(session, &removeIndex)) {
        uint32_t meshCount = 0;
        if (VKRT_getMeshCount(vkrt, &meshCount) != VKRT_SUCCESS) return;
        if (removeIndex < meshCount) {
            VKRT_Result result = VKRT_removeMesh(vkrt, removeIndex);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Removing mesh failed (%d)", (int)result);
            } else {
                sessionRemoveMeshReferences(session, removeIndex);
            }
        }
    }
}


void meshControllerLoadDefaultAssets(VKRT* vkrt, Session* session) {
    const char* planePath = "assets/models/plane.glb";
    const char* spherePath = "assets/models/sphere.glb";
    const char* dragonPath = "assets/models/dragon.glb";

    Material neutralWhite = VKRT_materialDefault();
    neutralWhite.roughness = 1.0f;
    neutralWhite.diffuseRoughness = 0.0f;

    Material leftWallMaterial = neutralWhite;
    leftWallMaterial.baseColor[0] = 0.80f;
    leftWallMaterial.baseColor[1] = 0.14f;
    leftWallMaterial.baseColor[2] = 0.12f;

    Material rightWallMaterial = neutralWhite;
    rightWallMaterial.baseColor[0] = 0.13f;
    rightWallMaterial.baseColor[1] = 0.70f;
    rightWallMaterial.baseColor[2] = 0.16f;

    Material lightMaterial = neutralWhite;
    lightMaterial.emissionColor[0] = 1.0f;
    lightMaterial.emissionColor[1] = 1.0f;
    lightMaterial.emissionColor[2] = 1.0f;
    lightMaterial.emissionLuminance = 30.0f;

    Material dragonMaterial = VKRT_materialDefault();
    dragonMaterial.baseColor[0] = 0.74f;
    dragonMaterial.baseColor[1] = 0.73f;
    dragonMaterial.baseColor[2] = 0.72f;
    dragonMaterial.roughness = 0.0f;
    dragonMaterial.metallic = 0.0f;

    Material sphereMaterial = VKRT_materialDefault();
    sphereMaterial.baseColor[0] = 1.0f;
    sphereMaterial.baseColor[1] = 1.0f;
    sphereMaterial.baseColor[2] = 1.0f;
    sphereMaterial.roughness = 0.0f;
    sphereMaterial.metallic = 1.0f;

    const DefaultMeshSpec defaultMeshes[] = {
        {
            .assetPath = planePath,
            .importName = "cornell_floor",
            .position = {0.0f, 0.0f, -1.0f},
            .rotation = {0.0f, 180.0f, -90.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .material = neutralWhite,
        },
        {
            .assetPath = planePath,
            .importName = "cornell_ceiling",
            .position = {0.0f, 0.0f, 1.0f},
            .rotation = {0.0f, 0.0f, -90.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .material = neutralWhite,
        },
        {
            .assetPath = planePath,
            .importName = "cornell_back_wall",
            .position = {0.0f, 1.0f, 0.0f},
            .rotation = {0.0f, 90.0f, -90.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .material = neutralWhite,
        },
        {
            .assetPath = planePath,
            .importName = "cornell_left_wall",
            .position = {-1.0f, 0.0f, 0.0f},
            .rotation = {-90.0f, 0.0f, -90.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .material = leftWallMaterial,
        },
        {
            .assetPath = planePath,
            .importName = "cornell_right_wall",
            .position = {1.0f, 0.0f, 0.0f},
            .rotation = {90.0f, 0.0f, -90.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .material = rightWallMaterial,
        },
        {
            .assetPath = spherePath,
            .importName = "cornell_light",
            .position = {0.0f, 0.0f, 1.0f},
            .rotation = {0.0f, 0.0f, 0.0f},
            .scale = {0.2f, 0.2f, 0.05f},
            .material = lightMaterial,
        },
        {
            .assetPath = dragonPath,
            .importName = "dragon",
            .position = {-0.1f, 0.32f, -1.45f},
            .rotation = {90.0f, 0.0f, 35.0f},
            .scale = {8.0f, 8.0f, 8.0f},
            .material = dragonMaterial,
        },
        {
            .assetPath = spherePath,
            .importName = "sphere",
            .position = {0.42f, -0.15f, -0.70f},
            .rotation = {0.0f, 0.0f, 0.0f},
            .scale = {0.3f, 0.3f, 0.3f},
            .material = sphereMaterial,
        },
    };

    for (size_t i = 0; i < sizeof(defaultMeshes) / sizeof(defaultMeshes[0]); i++) {
        importDefaultMesh(vkrt, session, &defaultMeshes[i]);
    }

    vec3 cameraPosition = {0.0f, -5.17f, 0.0f};
    vec3 cameraTarget = {0.0f, 0.0f, 0.0f};
    vec3 cameraUp = {0.0f, 0.0f, 1.0f};
    VKRT_Result result = VKRT_cameraSetPose(vkrt, cameraPosition, cameraTarget, cameraUp, 26.9f);
    if (result != VKRT_SUCCESS) LOG_ERROR("Setting default camera pose failed (%d)", (int)result);
}


/*
void meshControllerLoadDefaultAssets(VKRT* vkrt, Session* session) {
    const char* planePath = "assets/models/plane.glb";
    const char* spherePath = "assets/models/sphere.glb";
    const char* suzannePath = "assets/models/suzanne.glb";

    Material neutralWhite = VKRT_materialDefault();
    neutralWhite.roughness = 1.0f;
    neutralWhite.diffuseRoughness = 1.0f;

    Material floorMaterial = neutralWhite;
    floorMaterial.baseColor[0] = 1.00f;
    floorMaterial.baseColor[1] = 0.37f;
    floorMaterial.baseColor[2] = 0.00f;

    Material lightMaterial = neutralWhite;
    lightMaterial.baseColor[0] = 0.0f;
    lightMaterial.baseColor[1] = 0.0f;
    lightMaterial.baseColor[2] = 0.0f;
    lightMaterial.emissionColor[0] = 1.0f;
    lightMaterial.emissionColor[1] = 1.0f;
    lightMaterial.emissionColor[2] = 1.0f;
    lightMaterial.emissionLuminance = 8000.0f;

    Material suzanneMaterial = neutralWhite;
    suzanneMaterial.baseColor[0] = 1.0f;
    suzanneMaterial.baseColor[1] = 1.0f;
    suzanneMaterial.baseColor[2] = 1.0f;
    suzanneMaterial.roughness = 0.0f;
    suzanneMaterial.diffuseRoughness = 0.0f;
    suzanneMaterial.transmission = 1.0f;


    const DefaultMeshSpec defaultMeshes[] = {
        {
            .assetPath = planePath,
            .importName = "floor",
            .position = {0.0f, 0.0f, 0.0f},
            .rotation = {0.0f, 0.0f, 0.0f},
            .scale = {1000.0f, 1000.0f, 1.0f},
            .material = floorMaterial,
        },
        {
            .assetPath = spherePath,
            .importName = "light",
            .position = {1.95f, -1.4f, 4.30f},
            .rotation = {0.0f, 0.0f, 0.0f},
            .scale = {0.1f, 0.1f, 0.1f},
            .material = lightMaterial,
        },
        {
            .assetPath = suzannePath,
            .importName = "suzanne",
            .position = {0.0f, 0.01f, 0.5f},
            .rotation = {0.0f, 0.0f, 109.95f},
            .scale = {1.0f, 1.0f, 1.0f},
            .material = suzanneMaterial,
        }
    };

    for (size_t i = 0; i < sizeof(defaultMeshes) / sizeof(defaultMeshes[0]); i++) {
        importDefaultMesh(vkrt, session, &defaultMeshes[i]);
    }

    vec3 cameraPosition = {-5.973f, 2.651f, -2.664f};
    vec3 cameraTarget = {0.0f, -0.260f, 0.500f};
    vec3 cameraUp = {0.0f, 0.0f, 1.0f};
    VKRT_Result result = VKRT_cameraSetPose(vkrt, cameraPosition, cameraTarget, cameraUp, 26.9f);
    if (result != VKRT_SUCCESS) LOG_ERROR("Setting default camera pose failed (%d)", (int)result);

    vec3 environmentColor = {0.0f, 0.0f, 0.0f};
    float environmentStrength = 0.0f;
    result = VKRT_setEnvironmentLight(vkrt, environmentColor, environmentStrength);
    if (result != VKRT_SUCCESS) LOG_ERROR("Setting default environment light failed (%d)", (int)result);
}
*/
