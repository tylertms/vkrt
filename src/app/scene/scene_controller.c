#include "scene_controller.h"

#include "mesh_asset_loader.h"
#include "debug.h"

#include <stdint.h>
#include <stdio.h>

void sceneControllerImportMesh(VKRT* runtime, EditorState* state, const char* path) {
    if (!path || !path[0]) return;

    uint64_t startTime = getMicroseconds();
    uint32_t meshCountBeforeImport = VKRT_getMeshCount(runtime);
    meshAssetLoadFromFile(runtime, path);

    if (VKRT_getMeshCount(runtime) > meshCountBeforeImport) {
        editorStateSetMeshName(state, path, meshCountBeforeImport);
    }

    printf("[INFO]: Mesh import complete. File: %s, Total Meshes: %u, in %.3f ms\n",
        path,
        VKRT_getMeshCount(runtime),
        (double)(getMicroseconds() - startTime) / 1e3);
}

void sceneControllerApplyPendingActions(VKRT* runtime, EditorState* state) {
    if (state->pendingMeshImportPath) {
        sceneControllerImportMesh(runtime, state, state->pendingMeshImportPath);
        editorStateClearQueuedMeshImport(state);
    }

    if (state->pendingMeshRemovalIndex != UINT32_MAX) {
        uint32_t meshCount = VKRT_getMeshCount(runtime);
        uint32_t removeIndex = state->pendingMeshRemovalIndex;
        state->pendingMeshRemovalIndex = UINT32_MAX;

        if (removeIndex < meshCount) {
            editorStateRemoveMeshName(state, removeIndex);
            VKRT_removeMesh(runtime, removeIndex);
        }
    }
}

void sceneControllerLoadDefaultAssets(VKRT* runtime, EditorState* state) {
    sceneControllerImportMesh(runtime, state, "assets/models/sphere.glb");
    sceneControllerImportMesh(runtime, state, "assets/models/dragon.glb");
}
