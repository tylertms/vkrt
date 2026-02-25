#include "controller.h"
#include "loader.h"
#include "debug.h"

#include <stdint.h>
#include <stdlib.h>
#include <vkrt.h>

int meshControllerImportMesh(VKRT* vkrt, Session* session, const char* path, const char* importName, uint32_t* outMeshIndex) {
    if (outMeshIndex) *outMeshIndex = UINT32_MAX;
    if (!vkrt || !session || !path || !path[0]) return 0;

    uint64_t startTime = getMicroseconds();
    uint32_t meshCountBeforeImport = VKRT_getMeshCount(vkrt);
    meshLoadFromFile(vkrt, path);
    uint32_t meshCountAfterImport = VKRT_getMeshCount(vkrt);

    if (meshCountAfterImport <= meshCountBeforeImport) {
        LOG_ERROR("Mesh import failed. File: %s", path);
        return 0;
    }

    const char* meshName = (importName && importName[0]) ? importName : path;
    uint32_t meshIndex = meshCountBeforeImport;
    sessionSetMeshName(session, meshName, meshIndex);
    if (outMeshIndex) *outMeshIndex = meshIndex;

    LOG_INFO("Mesh import complete. File: %s, Total Meshes: %u, in %.3f ms",
        path,
        meshCountAfterImport,
        (double)(getMicroseconds() - startTime) / 1e3);
    return 1;
}

void meshControllerApplySessionActions(VKRT* vkrt, Session* session) {
    char* importPath = NULL;
    if (sessionTakeMeshImport(session, &importPath)) {
        meshControllerImportMesh(vkrt, session, importPath, NULL, NULL);
        free(importPath);
    }

    uint32_t removeIndex = UINT32_MAX;
    if (sessionTakeMeshRemoval(session, &removeIndex)) {
        uint32_t meshCount = VKRT_getMeshCount(vkrt);
        if (removeIndex < meshCount) {
            sessionRemoveMeshName(session, removeIndex);
            VKRT_removeMesh(vkrt, removeIndex);
        }
    }
}

void meshControllerLoadDefaultAssets(VKRT* vkrt, Session* session) {
    const char* planePath = "assets/models/plane.glb";
    const char* spherePath = "assets/models/sphere.glb";

    uint32_t floorMesh = UINT32_MAX;
    uint32_t ceilingMesh = UINT32_MAX;
    uint32_t backWallMesh = UINT32_MAX;
    uint32_t leftWallMesh = UINT32_MAX;
    uint32_t rightWallMesh = UINT32_MAX;
    uint32_t ceilingLightMesh = UINT32_MAX;

    meshControllerImportMesh(vkrt, session, planePath, "cornell_floor", &floorMesh);
    meshControllerImportMesh(vkrt, session, planePath, "cornell_ceiling", &ceilingMesh);
    meshControllerImportMesh(vkrt, session, planePath, "cornell_back_wall", &backWallMesh);
    meshControllerImportMesh(vkrt, session, planePath, "cornell_left_wall", &leftWallMesh);
    meshControllerImportMesh(vkrt, session, planePath, "cornell_right_wall", &rightWallMesh);
    meshControllerImportMesh(vkrt, session, spherePath, "cornell_light", &ceilingLightMesh);

    if (floorMesh != UINT32_MAX) {
        vec3 position = {0.0f, 0.0f, 1.0f};
        vec3 rotation = {0.0f, 180.0f, 0.0f};
        vec3 scale = {1.0f, 1.0f, 1.0f};
        VKRT_setMeshTransform(vkrt, floorMesh, position, rotation, scale);
    }

    if (ceilingMesh != UINT32_MAX) {
        vec3 position = {0.0f, 0.0f, -1.0f};
        vec3 rotation = {0.0f, 0.0f, 0.0f};
        vec3 scale = {1.0f, 1.0f, 1.0f};
        VKRT_setMeshTransform(vkrt, ceilingMesh, position, rotation, scale);
    }

    if (backWallMesh != UINT32_MAX) {
        vec3 position = {0.0f, 1.0f, 0.0f};
        vec3 rotation = {0.0f, 90.0f, 0.0f};
        vec3 scale = {1.0f, 1.0f, 1.0f};
        VKRT_setMeshTransform(vkrt, backWallMesh, position, rotation, scale);
    }

    if (leftWallMesh != UINT32_MAX) {
        vec3 position = {-1.0f, 0.0f, 0.0f};
        vec3 rotation = {-90.0f, 0.0f, 0.0f};
        vec3 scale = {1.0f, 1.0f, 1.0f};
        VKRT_setMeshTransform(vkrt, leftWallMesh, position, rotation, scale);
    }

    if (rightWallMesh != UINT32_MAX) {
        vec3 position = {1.0f, 0.0f, 0.0f};
        vec3 rotation = {90.0f, 0.0f, 0.0f};
        vec3 scale = {1.0f, 1.0f, 1.0f};
        VKRT_setMeshTransform(vkrt, rightWallMesh, position, rotation, scale);
    }

    if (ceilingLightMesh != UINT32_MAX) {
        vec3 position = {0.0f, 0.0f, -1.0f};
        vec3 rotation = {0.0f, 0.0f, 0.0f};
        vec3 scale = {0.2f, 0.05f, 0.2f};
        VKRT_setMeshTransform(vkrt, ceilingLightMesh, position, rotation, scale);
    }

    MaterialData neutralWhite = {
        .baseColor = {1.0f, 1.0f, 1.0f},
        .roughness = 1.0f,
        .specular = 0.0f,
        .emissionColor = {1.0f, 1.0f, 1.0f},
        .emissionStrength = 0.0f,
    };

    MaterialData leftWallMaterial = neutralWhite;
    leftWallMaterial.baseColor[0] = 0.80f;
    leftWallMaterial.baseColor[1] = 0.14f;
    leftWallMaterial.baseColor[2] = 0.12f;

    MaterialData rightWallMaterial = neutralWhite;
    rightWallMaterial.baseColor[0] = 0.13f;
    rightWallMaterial.baseColor[1] = 0.70f;
    rightWallMaterial.baseColor[2] = 0.16f;

    MaterialData lightMaterial = neutralWhite;
    lightMaterial.emissionColor[0] = 1.0f;
    lightMaterial.emissionColor[1] = 0.97f;
    lightMaterial.emissionColor[2] = 0.90f;
    lightMaterial.emissionStrength = 200.0f;

    if (floorMesh != UINT32_MAX) VKRT_setMeshMaterial(vkrt, floorMesh, &neutralWhite);
    if (ceilingMesh != UINT32_MAX) VKRT_setMeshMaterial(vkrt, ceilingMesh, &neutralWhite);
    if (backWallMesh != UINT32_MAX) VKRT_setMeshMaterial(vkrt, backWallMesh, &neutralWhite);
    if (leftWallMesh != UINT32_MAX) VKRT_setMeshMaterial(vkrt, leftWallMesh, &leftWallMaterial);
    if (rightWallMesh != UINT32_MAX) VKRT_setMeshMaterial(vkrt, rightWallMesh, &rightWallMaterial);
    if (ceilingLightMesh != UINT32_MAX) VKRT_setMeshMaterial(vkrt, ceilingLightMesh, &lightMaterial);

    uint32_t dragonMeshIndex = UINT32_MAX;
    if (meshControllerImportMesh(vkrt, session, "assets/models/dragon.glb", "dragon", &dragonMeshIndex)) {
        MaterialData dragonMaterial = {
            .baseColor = {0.74f, 0.73f, 0.72f},
            .roughness = 0.78f,
            .specular = 0.08f,
            .emissionColor = {1.0f, 1.0f, 1.0f},
            .emissionStrength = 0.0f,
        };

        vec3 dragonPosition = {0.0f, 0.0f, 0.59f};
        vec3 dragonRotation = {0.0f, 0.0f, 125.0f};
        vec3 dragonScale = {8.0f, 8.0f, 8.0f};
        VKRT_setMeshTransform(vkrt, dragonMeshIndex, dragonPosition, dragonRotation, dragonScale);
        VKRT_setMeshMaterial(vkrt, dragonMeshIndex, &dragonMaterial);
    }

    vec3 cameraPosition = {0.0f, -5.17, 0.0f};
    vec3 cameraTarget = {0.0f, 0.0f, 0.0f};
    vec3 cameraUp = {0.0f, 0.0f, 1.0f};
    VKRT_cameraSetPose(vkrt, cameraPosition, cameraTarget, cameraUp, 26.9f);

    VKRT_setFogDensity(vkrt, 0.15);
}
