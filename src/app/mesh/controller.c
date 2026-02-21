#include "controller.h"

#include "loader.h"
#include "debug.h"

#include <stdint.h>

void meshControllerImportMesh(VKRT* vkrt, Session* session, const char* path) {
    if (!path || !path[0]) return;

    uint64_t startTime = getMicroseconds();
    uint32_t meshCountBeforeImport = VKRT_getMeshCount(vkrt);
    meshLoadFromFile(vkrt, path);

    if (VKRT_getMeshCount(vkrt) > meshCountBeforeImport) {
        sessionSetMeshName(session, path, meshCountBeforeImport);
    }

    LOG_INFO("Mesh import complete. File: %s, Total Meshes: %u, in %.3f ms",
        path,
        VKRT_getMeshCount(vkrt),
        (double)(getMicroseconds() - startTime) / 1e3);
}

void meshControllerApplyPendingActions(VKRT* vkrt, Session* session) {
    if (session->pendingMeshImportPath) {
        meshControllerImportMesh(vkrt, session, session->pendingMeshImportPath);
        sessionClearQueuedMeshImport(session);
    }

    if (session->pendingMeshRemovalIndex != UINT32_MAX) {
        uint32_t meshCount = VKRT_getMeshCount(vkrt);
        uint32_t removeIndex = session->pendingMeshRemovalIndex;
        session->pendingMeshRemovalIndex = UINT32_MAX;

        if (removeIndex < meshCount) {
            sessionRemoveMeshName(session, removeIndex);
            VKRT_removeMesh(vkrt, removeIndex);
        }
    }
}

void meshControllerLoadDefaultAssets(VKRT* vkrt, Session* session) {
    meshControllerImportMesh(vkrt, session, "assets/models/sphere.glb");
    meshControllerImportMesh(vkrt, session, "assets/models/dragon.glb");

    MaterialData sphereMaterial = {
        .baseColor = {1.0f, 1.0f, 1.0f},
        .roughness = 1.0f,
        .specular = 0.0f,
        .emissionColor = {1.0f, 1.0f, 1.0f},
        .emissionStrength = 2.0f,
    };

    MaterialData dragonMaterial = {
        .baseColor = {0.72f, 0.18f, 0.16f},
        .roughness = 1.0f,
        .specular = 0.0f,
        .emissionColor = {1.0f, 1.0f, 1.0f},
        .emissionStrength = 0.0f,
    };

    if (VKRT_getMeshCount(vkrt) > 0) {
        VKRT_setMeshMaterial(vkrt, 0, &sphereMaterial);
    }

    if (VKRT_getMeshCount(vkrt) > 1) {
        VKRT_setMeshMaterial(vkrt, 1, &dragonMaterial);
    }
}
