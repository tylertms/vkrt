#include "scene_controller.h"

#include "mesh_asset_loader.h"

#include <stdint.h>

void sceneControllerImportMesh(VKRT* runtime, EditorState* state, const char* path) {
    if (!path || !path[0]) return;

    uint32_t meshCountBeforeImport = VKRT_getMeshCount(runtime);
    meshAssetLoadFromFile(runtime, path);

    if (VKRT_getMeshCount(runtime) > meshCountBeforeImport) {
        editorStateSetMeshName(state, path, meshCountBeforeImport);
    }
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
