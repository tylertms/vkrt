#include "controller.h"

#include "constants.h"
#include "debug.h"
#include "loader.h"
#include "session.h"
#include "vkrt.h"
#include "vkrt_types.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <types.h>

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

    for (uint32_t sortIndex = 1u; sortIndex < count; sortIndex++) {
        uint32_t value = meshIndices[sortIndex];
        uint32_t insertIndex = sortIndex;
        while (insertIndex > 0u && meshIndices[insertIndex - 1u] < value) {
            meshIndices[insertIndex] = meshIndices[insertIndex - 1u];
            insertIndex--;
        }
        meshIndices[insertIndex] = value;
    }
}

static int removeSceneObjectHierarchy(VKRT* vkrt, Session* session, uint32_t objectIndex) {
    if (!vkrt || !session || objectIndex >= sessionGetSceneObjectCount(session)) return 0;

    uint32_t objectCount = sessionGetSceneObjectCount(session);
    if (objectCount == 0u) return 0;

    uint32_t* meshIndices = (uint32_t*)malloc((size_t)objectCount * sizeof(uint32_t));
    if (!meshIndices) return 0;

    uint32_t meshCount = collectSceneObjectSubtreeMeshIndices(session, objectIndex, meshIndices, objectCount);
    sortMeshIndicesDescending(meshIndices, meshCount);

    for (uint32_t meshIndex = 0u; meshIndex < meshCount; meshIndex++) {
        VKRT_Result result = VKRT_removeMesh(vkrt, meshIndices[meshIndex]);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Removing scene object mesh failed (%d)", (int)result);
            free(meshIndices);
            return 0;
        }
        sessionRemoveMeshRecord(session, meshIndices[meshIndex]);
        sessionRemoveMeshReferencesNoPrune(session, meshIndices[meshIndex]);
    }

    sessionRemoveSceneObjectSubtree(session, objectIndex);

    free(meshIndices);
    return 1;
}

static void remapImportedMaterialTextures(Material* material, const uint32_t* textureIndexMap, uint32_t textureCount) {
    if (!material || !textureIndexMap) return;

    uint32_t* textureSlots[] = {
        &material->baseColorTextureIndex,
        &material->metallicRoughnessTextureIndex,
        &material->normalTextureIndex,
        &material->emissiveTextureIndex,
    };

    const uint32_t textureSlotCount = (uint32_t)(sizeof(textureSlots) / sizeof(textureSlots[0]));
    for (uint32_t textureSlotIndex = 0u; textureSlotIndex < textureSlotCount; textureSlotIndex++) {
        uint32_t* textureSlot = textureSlots[textureSlotIndex];
        if (*textureSlot == VKRT_INVALID_INDEX) {
            continue;
        }
        if (*textureSlot >= textureCount) {
            *textureSlot = VKRT_INVALID_INDEX;
            continue;
        }
        *textureSlot = textureIndexMap[*textureSlot];
    }
}

static int createImportedNodeObjects(Session* session, const MeshImportData* importData, uint32_t* nodeObjectIndices) {
    if (!session || !importData || (importData->nodeCount > 0u && !nodeObjectIndices)) return 0;

    for (uint32_t nodeIndex = 0u; nodeIndex < importData->nodeCount; nodeIndex++) {
        const NodeImportEntry* node = &importData->nodes[nodeIndex];
        uint32_t parentObjectIndex =
            node->parentIndex != VKRT_INVALID_INDEX ? nodeObjectIndices[node->parentIndex] : VKRT_INVALID_INDEX;
        const vec3 position = {node->position[0], node->position[1], node->position[2]};
        const vec3 rotation = {node->rotation[0], node->rotation[1], node->rotation[2]};
        const vec3 scale = {node->scale[0], node->scale[1], node->scale[2]};
        const char* nodeName = (node->name && node->name[0]) ? node->name : "Object";
        if (!sessionAddSceneObject(
                session,
                &(SessionSceneObjectCreateInfo){
                    .name = nodeName,
                    .parentIndex = parentObjectIndex,
                    .meshIndex = VKRT_INVALID_INDEX,
                    .localPosition = &position,
                    .localRotation = &rotation,
                    .localScale = &scale,
                },
                &nodeObjectIndices[nodeIndex]
            )) {
            return 0;
        }

        mat4 nodeLocalTransform = {
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 0.0f, 1.0f},
        };
        memcpy(nodeLocalTransform, node->localTransform, sizeof(nodeLocalTransform));
        if (!sessionSetSceneObjectLocalTransformMatrix(session, nodeObjectIndices[nodeIndex], nodeLocalTransform)) {
            return 0;
        }
    }

    return 1;
}

static const char* queryImportedObjectName(
    const MeshImportData* importData,
    const MeshImportEntry* entry,
    const char* importName,
    uint32_t nodeIndex
) {
    if (importData && importData->count == 1u && importName && importName[0]) return importName;
    if (entry && entry->name && entry->name[0]) return entry->name;
    if (nodeIndex != VKRT_INVALID_INDEX && importData && nodeIndex < importData->nodeCount &&
        importData->nodes[nodeIndex].name && importData->nodes[nodeIndex].name[0]) {
        return importData->nodes[nodeIndex].name;
    }
    return "Mesh";
}

static int attachImportedMeshesToSceneObjects(
    VKRT* vkrt,
    Session* session,
    const MeshImportData* importData,
    uint32_t meshIndexBase,
    const char* importName,
    const uint32_t* nodeObjectIndices
) {
    if (!vkrt || !session || !importData) return 0;

    const vec3 zero = {0.0f, 0.0f, 0.0f};
    const vec3 one = {1.0f, 1.0f, 1.0f};
    for (uint32_t entryIndex = 0u; entryIndex < importData->count; entryIndex++) {
        const MeshImportEntry* entry = &importData->entries[entryIndex];
        uint32_t meshIndex = meshIndexBase + entryIndex;
        uint32_t nodeIndex = entry->nodeIndex;
        if (nodeIndex != VKRT_INVALID_INDEX && nodeIndex < importData->nodeCount &&
            importData->nodes[nodeIndex].meshEntryCount == 1u) {
            if (!sessionSetSceneObjectMesh(session, nodeObjectIndices[nodeIndex], meshIndex)) {
                return 0;
            }
            if (importData->count == 1u && importName && importName[0]) {
                sessionSetSceneObjectName(session, nodeObjectIndices[nodeIndex], importName);
            } else if (
                (!importData->nodes[nodeIndex].name || !importData->nodes[nodeIndex].name[0]) && entry->name &&
                entry->name[0]
            ) {
                sessionSetSceneObjectName(session, nodeObjectIndices[nodeIndex], entry->name);
            }
            continue;
        }

        uint32_t parentObjectIndex = VKRT_INVALID_INDEX;
        if (nodeIndex != VKRT_INVALID_INDEX && nodeIndex < importData->nodeCount) {
            parentObjectIndex = nodeObjectIndices[nodeIndex];
        }

        const char* objectName = queryImportedObjectName(importData, entry, importName, nodeIndex);
        if (!sessionAddSceneObject(
                session,
                &(SessionSceneObjectCreateInfo){
                    .name = objectName,
                    .parentIndex = parentObjectIndex,
                    .meshIndex = meshIndex,
                    .localPosition = &zero,
                    .localRotation = &zero,
                    .localScale = &one,
                },
                NULL
            )) {
            return 0;
        }
    }

    return sessionSyncSceneObjectTransforms(vkrt, session);
}

static int buildImportedSceneObjects(
    VKRT* vkrt,
    Session* session,
    const MeshImportData* importData,
    uint32_t meshIndexBase,
    const char* importName
) {
    if (!vkrt || !session || !importData) return 0;

    uint32_t* nodeObjectIndices =
        importData->nodeCount > 0 ? (uint32_t*)malloc(importData->nodeCount * sizeof(uint32_t)) : NULL;
    if (importData->nodeCount > 0 && !nodeObjectIndices) {
        return 0;
    }

    if (!createImportedNodeObjects(session, importData, nodeObjectIndices) ||
        !attachImportedMeshesToSceneObjects(vkrt, session, importData, meshIndexBase, importName, nodeObjectIndices)) {
        free(nodeObjectIndices);
        return 0;
    }

    free(nodeObjectIndices);
    return 1;
}

static int allocateImportedIndexMaps(
    const MeshImportData* importData,
    uint32_t** outImportedTextureIndices,
    uint32_t** outImportedMaterialIndices
) {
    if (outImportedTextureIndices) *outImportedTextureIndices = NULL;
    if (outImportedMaterialIndices) *outImportedMaterialIndices = NULL;
    if (!importData || !outImportedTextureIndices || !outImportedMaterialIndices) return 0;

    if (importData->textureCount > 0u) {
        *outImportedTextureIndices = (uint32_t*)malloc(importData->textureCount * sizeof(uint32_t));
        if (!*outImportedTextureIndices) return 0;
        for (uint32_t textureIndex = 0u; textureIndex < importData->textureCount; textureIndex++) {
            (*outImportedTextureIndices)[textureIndex] = VKRT_INVALID_INDEX;
        }
    }

    if (importData->materialCount > 0u) {
        *outImportedMaterialIndices = (uint32_t*)malloc(importData->materialCount * sizeof(uint32_t));
        if (!*outImportedMaterialIndices) {
            free(*outImportedTextureIndices);
            *outImportedTextureIndices = NULL;
            return 0;
        }
        for (uint32_t materialIndex = 0u; materialIndex < importData->materialCount; materialIndex++) {
            (*outImportedMaterialIndices)[materialIndex] = VKRT_INVALID_INDEX;
        }
    }

    return 1;
}

static int uploadImportedMeshGeometry(VKRT* vkrt, const MeshImportData* importData) {
    if (!vkrt || !importData) return 0;
    if (importData->count == 0u) return 1;

    VKRT_MeshUpload* uploads = (VKRT_MeshUpload*)calloc(importData->count, sizeof(VKRT_MeshUpload));
    if (!uploads) return 0;

    for (uint32_t entryIndex = 0u; entryIndex < importData->count; entryIndex++) {
        const MeshImportEntry* entry = &importData->entries[entryIndex];
        uploads[entryIndex].vertices = entry->vertices;
        uploads[entryIndex].vertexCount = entry->vertexCount;
        uploads[entryIndex].indices = entry->indices;
        uploads[entryIndex].indexCount = entry->indexCount;
    }

    VKRT_Result result = VKRT_uploadMeshDataBatch(vkrt, uploads, importData->count);
    free(uploads);
    return result == VKRT_SUCCESS;
}

static int uploadImportedTextures(VKRT* vkrt, const MeshImportData* importData, uint32_t* importedTextureIndices) {
    if (!vkrt || !importData) return 0;
    if (importData->textureCount == 0u) return 1;
    if (!importedTextureIndices) return 0;

    VKRT_TextureUpload* textureUploads =
        (VKRT_TextureUpload*)calloc(importData->textureCount, sizeof(VKRT_TextureUpload));
    if (!textureUploads) return 0;

    for (uint32_t textureIndex = 0u; textureIndex < importData->textureCount; textureIndex++) {
        const TextureImportEntry* texture = &importData->textures[textureIndex];
        textureUploads[textureIndex] = (VKRT_TextureUpload){
            .name = texture->name,
            .pixels = texture->pixels,
            .width = texture->width,
            .height = texture->height,
            .format = texture->format,
            .colorSpace = texture->colorSpace,
        };
    }

    VKRT_Result result = VKRT_addTexturesBatch(vkrt, textureUploads, importData->textureCount, importedTextureIndices);
    free(textureUploads);
    return result == VKRT_SUCCESS;
}

static int uploadImportedMaterials(
    VKRT* vkrt,
    const MeshImportData* importData,
    const uint32_t* importedTextureIndices,
    uint32_t* importedMaterialIndices
) {
    if (!vkrt || !importData) return 0;
    if (importData->materialCount == 0u) return 1;
    if (!importedMaterialIndices) return 0;

    for (uint32_t materialIndex = 0u; materialIndex < importData->materialCount; materialIndex++) {
        const MaterialImportEntry* material = &importData->materials[materialIndex];
        Material remappedMaterial = material->material;
        remapImportedMaterialTextures(&remappedMaterial, importedTextureIndices, importData->textureCount);
        if (VKRT_addMaterial(vkrt, &remappedMaterial, material->name, &importedMaterialIndices[materialIndex]) !=
            VKRT_SUCCESS) {
            return 0;
        }
    }

    return 1;
}

static int configureImportedMeshes(
    VKRT* vkrt,
    const MeshImportData* importData,
    const uint32_t* importedMaterialIndices,
    uint32_t meshCountBeforeImport,
    const char* path,
    const char* importName,
    uint32_t* outConfiguredMeshCount
) {
    if (outConfiguredMeshCount) *outConfiguredMeshCount = 0u;
    if (!vkrt || !importData || !outConfiguredMeshCount) return 0;

    for (uint32_t entryIndex = 0u; entryIndex < importData->count; entryIndex++) {
        const MeshImportEntry* entry = &importData->entries[entryIndex];
        uint32_t meshIndex = meshCountBeforeImport + *outConfiguredMeshCount;

        VKRT_Result materialResult = VKRT_SUCCESS;
        if (entry->materialIndex != VKRT_INVALID_INDEX) {
            if (!importedMaterialIndices) return 0;
            materialResult = VKRT_setMeshMaterialIndex(vkrt, meshIndex, importedMaterialIndices[entry->materialIndex]);
        }

        if (materialResult != VKRT_SUCCESS ||
            VKRT_setMeshRenderBackfaces(vkrt, meshIndex, entry->renderBackfaces) != VKRT_SUCCESS) {
            return 0;
        }

        const char* meshName = entry->name;
        if (importName && importName[0] && importData->count == 1u) meshName = importName;
        if (!meshName || !meshName[0]) meshName = path;
        if (VKRT_setMeshName(vkrt, meshIndex, meshName) != VKRT_SUCCESS) {
            LOG_ERROR("Mesh imported but failed to store mesh label. File: %s", meshName);
        }

        (*outConfiguredMeshCount)++;
    }

    return 1;
}

static int finalizeImportedAssetRegistration(
    VKRT* vkrt,
    Session* session,
    const MeshImportData* importData,
    uint32_t meshCountBeforeImport,
    uint32_t textureRecordCountBeforeImport,
    uint32_t objectCountBeforeImport,
    const char* importName
) {
    if (!buildImportedSceneObjects(vkrt, session, importData, meshCountBeforeImport, importName)) return 0;

    if (!sessionAppendImportedTextureRecords(session, importData->textureCount)) {
        sessionTruncateTextureRecords(session, textureRecordCountBeforeImport);
        rollbackImportedSceneObjects(session, objectCountBeforeImport);
        return 0;
    }

    return 1;
}

static int queryImportBaselineCounts(
    VKRT* vkrt,
    Session* session,
    uint32_t* outMeshCountBeforeImport,
    uint32_t* outMaterialCountBeforeImport,
    uint32_t* outTextureCountBeforeImport,
    uint32_t* outObjectCountBeforeImport,
    uint32_t* outTextureRecordCountBeforeImport
) {
    if (!vkrt || !session || !outMeshCountBeforeImport || !outMaterialCountBeforeImport ||
        !outTextureCountBeforeImport || !outObjectCountBeforeImport || !outTextureRecordCountBeforeImport) {
        return 0;
    }

    *outObjectCountBeforeImport = sessionGetSceneObjectCount(session);
    *outTextureRecordCountBeforeImport = sessionGetTextureRecordCount(session);
    return VKRT_getMeshCount(vkrt, outMeshCountBeforeImport) == VKRT_SUCCESS &&
           VKRT_getMaterialCount(vkrt, outMaterialCountBeforeImport) == VKRT_SUCCESS &&
           VKRT_getTextureCount(vkrt, outTextureCountBeforeImport) == VKRT_SUCCESS;
}

static void cleanupImportedIndexMaps(uint32_t* importedTextureIndices, uint32_t* importedMaterialIndices) {
    free(importedTextureIndices);
    free(importedMaterialIndices);
}

static void rollbackFailedImport(
    VKRT* vkrt,
    Session* session,
    uint32_t meshCountBeforeImport,
    uint32_t materialCountBeforeImport,
    uint32_t textureCountBeforeImport,
    uint32_t objectCountBeforeImport
) {
    rollbackImportedSceneObjects(session, objectCountBeforeImport);
    rollbackImportedMeshes(vkrt, meshCountBeforeImport);
    rollbackImportedMaterials(vkrt, materialCountBeforeImport);
    rollbackImportedTextures(vkrt, textureCountBeforeImport);
}

static int verifyImportedMeshCount(VKRT* vkrt, uint32_t meshCountBeforeImport, const char* path) {
    uint32_t meshCountAfterImport = 0u;
    if (VKRT_getMeshCount(vkrt, &meshCountAfterImport) != VKRT_SUCCESS ||
        meshCountAfterImport <= meshCountBeforeImport) {
        LOG_ERROR("Mesh import failed. File: %s", path);
        return 0;
    }

    LOG_INFO(
        "Mesh import complete. File: %s, Imported Meshes: %u, Total Meshes: %u",
        path,
        meshCountAfterImport - meshCountBeforeImport,
        meshCountAfterImport
    );
    return 1;
}

int meshControllerImportMesh(
    VKRT* vkrt,
    Session* session,
    const char* path,
    const char* importName,
    uint32_t* outMeshIndex
) {
    if (outMeshIndex) *outMeshIndex = VKRT_INVALID_INDEX;
    if (!vkrt || !session || !path || !path[0]) return 0;

    uint32_t meshCountBeforeImport = 0;
    uint32_t materialCountBeforeImport = 0;
    uint32_t textureCountBeforeImport = 0;
    uint32_t objectCountBeforeImport = 0u;
    uint32_t textureRecordCountBeforeImport = 0u;
    if (!queryImportBaselineCounts(
            vkrt,
            session,
            &meshCountBeforeImport,
            &materialCountBeforeImport,
            &textureCountBeforeImport,
            &objectCountBeforeImport,
            &textureRecordCountBeforeImport
        )) {
        return 0;
    }

    MeshImportData importData = {0};
    if (meshLoadFromFile(path, &importData) != 0) {
        LOG_ERROR("Mesh import failed. File: %s", path);
        return 0;
    }

    uint32_t* importedTextureIndices = NULL;
    uint32_t* importedMaterialIndices = NULL;
    if (!allocateImportedIndexMaps(&importData, &importedTextureIndices, &importedMaterialIndices)) {
        meshReleaseImportData(&importData);
        return 0;
    }

    if (!uploadImportedMeshGeometry(vkrt, &importData)) {
        LOG_ERROR("Mesh upload failed. File: %s", path);
        rollbackImportedMeshes(vkrt, meshCountBeforeImport);
        cleanupImportedIndexMaps(importedTextureIndices, importedMaterialIndices);
        meshReleaseImportData(&importData);
        return 0;
    }

    if (!uploadImportedTextures(vkrt, &importData, importedTextureIndices)) {
        LOG_ERROR("Texture import failed. File: %s", path);
        rollbackImportedMeshes(vkrt, meshCountBeforeImport);
        rollbackImportedTextures(vkrt, textureCountBeforeImport);
        cleanupImportedIndexMaps(importedTextureIndices, importedMaterialIndices);
        meshReleaseImportData(&importData);
        return 0;
    }

    if (!uploadImportedMaterials(vkrt, &importData, importedTextureIndices, importedMaterialIndices)) {
        LOG_ERROR("Material import failed. File: %s", path);
        rollbackImportedMeshes(vkrt, meshCountBeforeImport);
        rollbackImportedMaterials(vkrt, materialCountBeforeImport);
        rollbackImportedTextures(vkrt, textureCountBeforeImport);
        cleanupImportedIndexMaps(importedTextureIndices, importedMaterialIndices);
        meshReleaseImportData(&importData);
        return 0;
    }

    uint32_t configuredMeshCount = 0u;
    if (!configureImportedMeshes(
            vkrt,
            &importData,
            importedMaterialIndices,
            meshCountBeforeImport,
            path,
            importName,
            &configuredMeshCount
        ) ||
        !finalizeImportedAssetRegistration(
            vkrt,
            session,
            &importData,
            meshCountBeforeImport,
            textureRecordCountBeforeImport,
            objectCountBeforeImport,
            importName
        )) {
        rollbackFailedImport(
            vkrt,
            session,
            meshCountBeforeImport,
            materialCountBeforeImport,
            textureCountBeforeImport,
            objectCountBeforeImport
        );
        cleanupImportedIndexMaps(importedTextureIndices, importedMaterialIndices);
        meshReleaseImportData(&importData);
        return 0;
    }

    if (!sessionRegisterMeshImportBatch(session, path, configuredMeshCount)) {
        sessionTruncateTextureRecords(session, textureRecordCountBeforeImport);
        rollbackFailedImport(
            vkrt,
            session,
            meshCountBeforeImport,
            materialCountBeforeImport,
            textureCountBeforeImport,
            objectCountBeforeImport
        );
        cleanupImportedIndexMaps(importedTextureIndices, importedMaterialIndices);
        meshReleaseImportData(&importData);
        return 0;
    }

    if (outMeshIndex) *outMeshIndex = configuredMeshCount > 0u ? meshCountBeforeImport : VKRT_INVALID_INDEX;

    cleanupImportedIndexMaps(importedTextureIndices, importedMaterialIndices);
    meshReleaseImportData(&importData);

    if (!verifyImportedMeshCount(vkrt, meshCountBeforeImport, path)) {
        rollbackImportedMeshes(vkrt, meshCountBeforeImport);
        rollbackImportedMaterials(vkrt, materialCountBeforeImport);
        rollbackImportedTextures(vkrt, textureCountBeforeImport);
        return 0;
    }
    return 1;
}

static void applyQueuedMeshImport(VKRT* vkrt, Session* session) {
    char* importPath = NULL;
    if (!sessionTakeMeshImport(session, &importPath)) return;
    meshControllerImportMesh(vkrt, session, importPath, NULL, NULL);
    free(importPath);
}

static void applyQueuedTextureImport(VKRT* vkrt, Session* session) {
    char* textureImportPath = NULL;
    uint32_t materialIndex = VKRT_INVALID_INDEX;
    uint32_t textureSlot = VKRT_INVALID_INDEX;
    if (!sessionTakeTextureImport(session, &materialIndex, &textureSlot, &textureImportPath)) return;

    uint32_t textureIndex = VKRT_INVALID_INDEX;
    uint32_t colorSpace = colorSpaceForTextureSlot(textureSlot);
    VKRT_MaterialSnapshot previousMaterial = {0};
    VKRT_Result previousMaterialResult = VKRT_getMaterialSnapshot(vkrt, materialIndex, &previousMaterial);
    VKRT_Result result = VKRT_addTextureFromFile(vkrt, textureImportPath, NULL, colorSpace, &textureIndex);
    if (result == VKRT_SUCCESS) {
        result = VKRT_setMaterialTexture(vkrt, materialIndex, textureSlot, textureIndex);
        if (result == VKRT_SUCCESS) {
            if (!sessionAppendStandaloneTextureRecord(session, textureImportPath, colorSpace)) {
                if (previousMaterialResult == VKRT_SUCCESS) {
                    (void)VKRT_setMaterial(vkrt, materialIndex, &previousMaterial.material);
                }
                (void)VKRT_removeTexture(vkrt, textureIndex);
                result = VKRT_ERROR_OUT_OF_MEMORY;
            }
        } else {
            (void)VKRT_removeTexture(vkrt, textureIndex);
        }
    }
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Texture import failed (%d). File: %s", (int)result, textureImportPath);
    }
    free(textureImportPath);
}

static void applyQueuedEnvironmentImport(VKRT* vkrt, Session* session) {
    char* environmentImportPath = NULL;
    if (!sessionTakeEnvironmentImport(session, &environmentImportPath)) return;

    VKRT_Result result = VKRT_setEnvironmentTextureFromFile(vkrt, environmentImportPath);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Environment import failed (%d). File: %s", (int)result, environmentImportPath);
    } else {
        sessionSetEnvironmentTexturePath(session, environmentImportPath);
    }
    free(environmentImportPath);
}

static void applyQueuedEnvironmentClear(VKRT* vkrt, Session* session) {
    if (!sessionTakeEnvironmentClear(session)) return;

    VKRT_Result result = VKRT_clearEnvironmentTexture(vkrt);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Clearing environment texture failed (%d)", (int)result);
    } else {
        sessionClearEnvironmentTexturePath(session);
    }
}

static void applyQueuedSceneObjectRemoval(VKRT* vkrt, Session* session) {
    uint32_t removeObjectIndex = VKRT_INVALID_INDEX;
    if (!sessionTakeSceneObjectRemoval(session, &removeObjectIndex)) return;
    if (!removeSceneObjectHierarchy(vkrt, session, removeObjectIndex)) {
        LOG_ERROR("Removing scene object hierarchy failed");
    }
}

static void applyQueuedMeshRemoval(VKRT* vkrt, Session* session) {
    uint32_t removeIndex = VKRT_INVALID_INDEX;
    if (!sessionTakeMeshRemoval(session, &removeIndex)) return;

    uint32_t meshCount = 0;
    if (VKRT_getMeshCount(vkrt, &meshCount) != VKRT_SUCCESS) return;
    if (removeIndex < meshCount) {
        VKRT_Result result = VKRT_removeMesh(vkrt, removeIndex);
        if (result != VKRT_SUCCESS) {
            LOG_ERROR("Removing mesh failed (%d)", (int)result);
        } else {
            sessionRemoveMeshRecord(session, removeIndex);
            sessionRemoveMeshReferences(session, removeIndex);
        }
    }
}

void meshControllerApplySessionActions(VKRT* vkrt, Session* session) {
    applyQueuedMeshImport(vkrt, session);
    applyQueuedTextureImport(vkrt, session);
    applyQueuedEnvironmentImport(vkrt, session);
    applyQueuedEnvironmentClear(vkrt, session);
    applyQueuedSceneObjectRemoval(vkrt, session);
    applyQueuedMeshRemoval(vkrt, session);
}
