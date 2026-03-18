#include "controller.h"
#include "loader.h"
#include "debug.h"
#include "vkrt.h"
#include "vkrt_types.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct DefaultMeshSpec {
    const char* assetPath;
    const char* importName;
    vec3 position;
    vec3 rotation;
    vec3 scale;
    Material material;
} DefaultMeshSpec;

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

static void applyMeshTransform(VKRT* vkrt, uint32_t meshIndex, const DefaultMeshSpec* spec) {
    if (!vkrt || !spec || meshIndex == VKRT_INVALID_INDEX) return;

    vec3 position = {spec->position[0], spec->position[1], spec->position[2]};
    vec3 rotation = {spec->rotation[0], spec->rotation[1], spec->rotation[2]};
    vec3 scale = {spec->scale[0], spec->scale[1], spec->scale[2]};

    VKRT_Result result = VKRT_setMeshTransform(vkrt, meshIndex, position, rotation, scale);
    if (result != VKRT_SUCCESS) {
        LOG_ERROR("Setting %s transform failed (%d)", spec->importName, (int)result);
    }
}

static void applyMeshMaterial(VKRT* vkrt, uint32_t meshIndex, const DefaultMeshSpec* spec) {
    if (!vkrt || !spec || meshIndex == VKRT_INVALID_INDEX) return;

    VKRT_Result result = VKRT_setMeshMaterial(vkrt, meshIndex, &spec->material);
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

    applyMeshTransform(vkrt, meshIndex, spec);
    applyMeshMaterial(vkrt, meshIndex, spec);
}

int meshControllerImportMesh(VKRT* vkrt, Session* session, const char* path, const char* importName, uint32_t* outMeshIndex) {
    if (outMeshIndex) *outMeshIndex = VKRT_INVALID_INDEX;
    if (!vkrt || !session || !path || !path[0]) return 0;

    uint64_t startTime = getMicroseconds();
    uint32_t meshCountBeforeImport = 0;
    if (VKRT_getMeshCount(vkrt, &meshCountBeforeImport) != VKRT_SUCCESS) return 0;

    MeshImportData importData = {0};
    if (meshLoadFromFile(path, &importData) != 0) {
        LOG_ERROR("Mesh import failed. File: %s", path);
        return 0;
    }

    uint32_t importedCount = 0;
    for (uint32_t i = 0; i < importData.count; i++) {
        const MeshImportEntry* entry = &importData.entries[i];
        uint32_t meshIndex = meshCountBeforeImport + importedCount;

        if (VKRT_uploadMeshData(vkrt, entry->vertices, entry->vertexCount, entry->indices, entry->indexCount) != VKRT_SUCCESS) {
            LOG_ERROR("Mesh upload failed. File: %s, Entry: %s", path, entry->name ? entry->name : "(unnamed)");
            rollbackImportedMeshes(vkrt, meshCountBeforeImport);
            meshReleaseImportData(&importData);
            return 0;
        }

        if (VKRT_setMeshMaterial(vkrt, meshIndex, &entry->material) != VKRT_SUCCESS ||
            VKRT_setMeshRenderBackfaces(vkrt, meshIndex, entry->renderBackfaces) != VKRT_SUCCESS) {
            LOG_ERROR("Mesh material import failed. File: %s, Entry: %s", path, entry->name ? entry->name : "(unnamed)");
            rollbackImportedMeshes(vkrt, meshCountBeforeImport);
            meshReleaseImportData(&importData);
            return 0;
        }

        vec3 position = {entry->position[0], entry->position[1], entry->position[2]};
        vec3 rotation = {entry->rotation[0], entry->rotation[1], entry->rotation[2]};
        vec3 scale = {entry->scale[0], entry->scale[1], entry->scale[2]};
        if (VKRT_setMeshTransform(vkrt, meshIndex, position, rotation, scale) != VKRT_SUCCESS) {
            LOG_ERROR("Mesh transform import failed. File: %s, Entry: %s", path, entry->name ? entry->name : "(unnamed)");
            rollbackImportedMeshes(vkrt, meshCountBeforeImport);
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

        if (importedCount == 0 && outMeshIndex) {
            *outMeshIndex = meshIndex;
        }
        importedCount++;
    }

    meshReleaseImportData(&importData);

    uint32_t meshCountAfterImport = 0;
    if (VKRT_getMeshCount(vkrt, &meshCountAfterImport) != VKRT_SUCCESS || meshCountAfterImport <= meshCountBeforeImport) {
        LOG_ERROR("Mesh import failed. File: %s", path);
        rollbackImportedMeshes(vkrt, meshCountBeforeImport);
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

    uint32_t removeIndex = VKRT_INVALID_INDEX;
    if (sessionTakeMeshRemoval(session, &removeIndex)) {
        uint32_t meshCount = 0;
        if (VKRT_getMeshCount(vkrt, &meshCount) != VKRT_SUCCESS) return;
        if (removeIndex < meshCount) {
            VKRT_Result result = VKRT_removeMesh(vkrt, removeIndex);
            if (result != VKRT_SUCCESS) {
                LOG_ERROR("Removing mesh failed (%d)", (int)result);
            }
        }
    }
}

/*
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
    lightMaterial.emissionLuminance = 70.0f;

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
            .position = {0.0f, 0.0f, 1.0f},
            .rotation = {0.0f, 180.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .material = neutralWhite,
        },
        {
            .assetPath = planePath,
            .importName = "cornell_ceiling",
            .position = {0.0f, 0.0f, -1.0f},
            .rotation = {0.0f, 0.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .material = neutralWhite,
        },
        {
            .assetPath = planePath,
            .importName = "cornell_back_wall",
            .position = {0.0f, 1.0f, 0.0f},
            .rotation = {0.0f, 90.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .material = neutralWhite,
        },
        {
            .assetPath = planePath,
            .importName = "cornell_left_wall",
            .position = {-1.0f, 0.0f, 0.0f},
            .rotation = {-90.0f, 0.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .material = leftWallMaterial,
        },
        {
            .assetPath = planePath,
            .importName = "cornell_right_wall",
            .position = {1.0f, 0.0f, 0.0f},
            .rotation = {90.0f, 0.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f},
            .material = rightWallMaterial,
        },
        {
            .assetPath = spherePath,
            .importName = "cornell_light",
            .position = {0.0f, 0.0f, -1.0f},
            .rotation = {0.0f, 0.0f, 0.0f},
            .scale = {0.2f, 0.05f, 0.2f},
            .material = lightMaterial,
        },
        {
            .assetPath = dragonPath,
            .importName = "dragon",
            .position = {-0.1f, 0.32f, 1.45f},
            .rotation = {90.0f, 0.0f, 125.0f},
            .scale = {8.0f, 8.0f, 8.0f},
            .material = dragonMaterial,
        },
        {
            .assetPath = spherePath,
            .importName = "sphere",
            .position = {0.42f, -0.15f, 0.70f},
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
*/

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
    lightMaterial.emissionLuminance = 2000.0f;

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
            .position = {0.0f, 0.0f, 1.0f},
            .rotation = {0.0f, 0.0f, 90.0f},
            .scale = {1000.0f, 1.0f, 1000.0f},
            .material = floorMaterial,
        },
        {
            .assetPath = spherePath,
            .importName = "light",
            .position = {1.95f, 4.30f, -1.4f},
            .rotation = {90.0f, 0.0f, 90.0f},
            .scale = {0.2f, 0.2f, 0.2f},
            .material = lightMaterial,
        },
        {
            .assetPath = suzannePath,
            .importName = "suzanne",
            .position = {0.0f, 0.01f, 0.5f},
            .rotation = {35.75f, 180.0f, 109.95f},
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
