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
    uint32_t meshCountBeforeImport = 0;
    if (VKRT_getMeshCount(vkrt, &meshCountBeforeImport) != VKRT_SUCCESS) return 0;
    if (meshLoadFromFile(vkrt, path) != 0) {
        LOG_ERROR("Mesh import failed. File: %s", path);
        return 0;
    }
    uint32_t meshCountAfterImport = 0;
    if (VKRT_getMeshCount(vkrt, &meshCountAfterImport) != VKRT_SUCCESS) return 0;

    if (meshCountAfterImport <= meshCountBeforeImport) {
        LOG_ERROR("Mesh import failed. File: %s", path);
        return 0;
    }

    const char* meshName = (importName && importName[0]) ? importName : path;
    uint32_t meshIndex = meshCountBeforeImport;
    if (!sessionSetMeshName(session, meshName, meshIndex)) {
        LOG_ERROR("Mesh imported but failed to store mesh label. File: %s", meshName);
    }
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
        uint32_t meshCount = 0;
        if (VKRT_getMeshCount(vkrt, &meshCount) != VKRT_SUCCESS) return;
        if (removeIndex < meshCount) {
            VKRT_Result result = VKRT_removeMesh(vkrt, removeIndex);
            if (result == VKRT_SUCCESS) {
                sessionRemoveMeshName(session, removeIndex);
            } else {
                LOG_ERROR("Removing mesh failed (%d)", (int)result);
            }
        }
    }
}

void meshControllerLoadDefaultAssets(VKRT* vkrt, Session* session) {
    const char* planePath = "assets/models/plane.glb";
    const char* spherePath = "assets/models/sphere.glb";
    const char* dragonPath = "assets/models/dragon.glb";

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
        VKRT_Result result = VKRT_setMeshTransform(vkrt, floorMesh, position, rotation, scale);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting floor transform failed (%d)", (int)result);
    }

    if (ceilingMesh != UINT32_MAX) {
        vec3 position = {0.0f, 0.0f, -1.0f};
        vec3 rotation = {0.0f, 0.0f, 0.0f};
        vec3 scale = {1.0f, 1.0f, 1.0f};
        VKRT_Result result = VKRT_setMeshTransform(vkrt, ceilingMesh, position, rotation, scale);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting ceiling transform failed (%d)", (int)result);
    }

    if (backWallMesh != UINT32_MAX) {
        vec3 position = {0.0f, 1.0f, 0.0f};
        vec3 rotation = {0.0f, 90.0f, 0.0f};
        vec3 scale = {1.0f, 1.0f, 1.0f};
        VKRT_Result result = VKRT_setMeshTransform(vkrt, backWallMesh, position, rotation, scale);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting back wall transform failed (%d)", (int)result);
    }

    if (leftWallMesh != UINT32_MAX) {
        vec3 position = {-1.0f, 0.0f, 0.0f};
        vec3 rotation = {-90.0f, 0.0f, 0.0f};
        vec3 scale = {1.0f, 1.0f, 1.0f};
        VKRT_Result result = VKRT_setMeshTransform(vkrt, leftWallMesh, position, rotation, scale);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting left wall transform failed (%d)", (int)result);
    }

    if (rightWallMesh != UINT32_MAX) {
        vec3 position = {1.0f, 0.0f, 0.0f};
        vec3 rotation = {90.0f, 0.0f, 0.0f};
        vec3 scale = {1.0f, 1.0f, 1.0f};
        VKRT_Result result = VKRT_setMeshTransform(vkrt, rightWallMesh, position, rotation, scale);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting right wall transform failed (%d)", (int)result);
    }

    if (ceilingLightMesh != UINT32_MAX) {
        vec3 position = {0.0f, 0.0f, -1.0f};
        vec3 rotation = {0.0f, 0.0f, 0.0f};
        vec3 scale = {0.2f, 0.05f, 0.2f};
        VKRT_Result result = VKRT_setMeshTransform(vkrt, ceilingLightMesh, position, rotation, scale);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting ceiling light transform failed (%d)", (int)result);
    }

    MaterialData neutralWhite = VKRT_materialDataDisneyDefault();
    neutralWhite.specular = 0.0f;
    neutralWhite.roughness = 1.0f;

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
    lightMaterial.emissionColor[1] = 1.0f;
    lightMaterial.emissionColor[2] = 1.0f;
    lightMaterial.emissionLuminance = 70.0f;

    if (floorMesh != UINT32_MAX) {
        VKRT_Result result = VKRT_setMeshMaterial(vkrt, floorMesh, &neutralWhite);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting floor material failed (%d)", (int)result);
    }
    if (ceilingMesh != UINT32_MAX) {
        VKRT_Result result = VKRT_setMeshMaterial(vkrt, ceilingMesh, &neutralWhite);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting ceiling material failed (%d)", (int)result);
    }
    if (backWallMesh != UINT32_MAX) {
        VKRT_Result result = VKRT_setMeshMaterial(vkrt, backWallMesh, &neutralWhite);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting back wall material failed (%d)", (int)result);
    }
    if (leftWallMesh != UINT32_MAX) {
        VKRT_Result result = VKRT_setMeshMaterial(vkrt, leftWallMesh, &leftWallMaterial);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting left wall material failed (%d)", (int)result);
    }
    if (rightWallMesh != UINT32_MAX) {
        VKRT_Result result = VKRT_setMeshMaterial(vkrt, rightWallMesh, &rightWallMaterial);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting right wall material failed (%d)", (int)result);
    }
    if (ceilingLightMesh != UINT32_MAX) {
        VKRT_Result result = VKRT_setMeshMaterial(vkrt, ceilingLightMesh, &lightMaterial);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting ceiling light material failed (%d)", (int)result);
    }

    uint32_t dragonMeshIndex = UINT32_MAX;
    if (meshControllerImportMesh(vkrt, session, dragonPath, "dragon", &dragonMeshIndex)) {
        MaterialData dragonMaterial = VKRT_materialDataDisneyDefault();
        dragonMaterial.baseColor[0] = 0.74f;
        dragonMaterial.baseColor[1] = 0.73f;
        dragonMaterial.baseColor[2] = 0.72f;
        dragonMaterial.roughness = 0.0f;
        dragonMaterial.metallic = 0.0f;

        vec3 dragonPosition = {-0.1f, 0.32f, 0.59f};
        vec3 dragonRotation = {0.0f, 0.0f, 125.0f};
        vec3 dragonScale = {8.0f, 8.0f, 8.0f};
        VKRT_Result result = VKRT_setMeshTransform(vkrt, dragonMeshIndex, dragonPosition, dragonRotation, dragonScale);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting dragon transform failed (%d)", (int)result);
        result = VKRT_setMeshMaterial(vkrt, dragonMeshIndex, &dragonMaterial);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting dragon material failed (%d)", (int)result);
    }

    uint32_t sphereMeshIndex = UINT32_MAX;
    if (meshControllerImportMesh(vkrt, session, spherePath, "sphere", &sphereMeshIndex)) {
        MaterialData sphereMaterial = VKRT_materialDataDisneyDefault();
        sphereMaterial.baseColor[0] = 1.0f;
        sphereMaterial.baseColor[1] = 1.0f;
        sphereMaterial.baseColor[2] = 1.0f;
        sphereMaterial.roughness = 0.0f;
        sphereMaterial.metallic = 1.0f;

        vec3 spherePosition = {0.42f, -0.15f, 0.70f};
        vec3 sphereRotation = {0.0f, 0.0f, 0.0f};
        vec3 sphereScale = {0.3f, 0.3f, 0.3f};
        VKRT_Result result = VKRT_setMeshTransform(vkrt, sphereMeshIndex, spherePosition, sphereRotation, sphereScale);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting sphere transform failed (%d)", (int)result);
        result = VKRT_setMeshMaterial(vkrt, sphereMeshIndex, &sphereMaterial);
        if (result != VKRT_SUCCESS) LOG_ERROR("Setting sphere material failed (%d)", (int)result);
    }

    vec3 cameraPosition = {0.0f, -5.17, 0.0f};
    vec3 cameraTarget = {0.0f, 0.0f, 0.0f};
    vec3 cameraUp = {0.0f, 0.0f, 1.0f};
    VKRT_Result result = VKRT_cameraSetPose(vkrt, cameraPosition, cameraTarget, cameraUp, 26.9f);
    if (result != VKRT_SUCCESS) LOG_ERROR("Setting default camera pose failed (%d)", (int)result);

    result = VKRT_setFogDensity(vkrt, 0.0f);
    if (result != VKRT_SUCCESS) LOG_ERROR("Setting default fog density failed (%d)", (int)result);
}
