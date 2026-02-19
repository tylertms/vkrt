#pragma once

#include <stdint.h>

typedef struct DemoGUIState {
    char** meshLabels;
    uint32_t meshLabelCount;
    uint32_t pendingRemoveIndex;
    char* pendingAddPath;
} DemoGUIState;

void initDemoGUIState(DemoGUIState* state);
void deinitDemoGUIState(DemoGUIState* state);
void demoGUIOnMeshAdded(DemoGUIState* state, const char* filename, uint32_t meshIndex);
void demoGUIOnMeshRemoved(DemoGUIState* state, uint32_t removedIndex);
const char* demoGUIGetMeshLabel(const DemoGUIState* state, uint32_t meshIndex);

void demoGUIQueueAddMeshPath(DemoGUIState* state, const char* path);
void demoGUIClearPendingAddPath(DemoGUIState* state);
