#include "demo_actions.h"

#include "object.h"

#include <stdint.h>

void demoAddMeshFromPath(VKRT* vkrt, DemoGUIState* guiState, const char* path) {
    if (!path || !path[0]) return;

    uint32_t meshCountBeforeAdd = VKRT_getMeshCount(vkrt);
    loadObject(vkrt, path);
    if (VKRT_getMeshCount(vkrt) > meshCountBeforeAdd) {
        demoGUIOnMeshAdded(guiState, path, meshCountBeforeAdd);
    }
}

void demoProcessPendingActions(VKRT* vkrt, DemoGUIState* guiState) {
    if (guiState->pendingAddPath) {
        demoAddMeshFromPath(vkrt, guiState, guiState->pendingAddPath);
        demoGUIClearPendingAddPath(guiState);
    }

    if (guiState->pendingRemoveIndex != UINT32_MAX) {
        uint32_t meshCount = VKRT_getMeshCount(vkrt);
        uint32_t removeIndex = guiState->pendingRemoveIndex;
        guiState->pendingRemoveIndex = UINT32_MAX;
        if (removeIndex < meshCount) {
            demoGUIOnMeshRemoved(guiState, removeIndex);
            VKRT_removeMesh(vkrt, removeIndex);
        }
    }
}

void demoLoadInitialMeshes(VKRT* vkrt, DemoGUIState* guiState) {
    demoAddMeshFromPath(vkrt, guiState, "assets/models/sphere.glb");
    demoAddMeshFromPath(vkrt, guiState, "assets/models/dragon.glb");
}
