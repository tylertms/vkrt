#pragma once

#include "vkrt.h"
#include "demo_state.h"

void demoAddMeshFromPath(VKRT* vkrt, DemoGUIState* guiState, const char* path);
void demoProcessPendingActions(VKRT* vkrt, DemoGUIState* guiState);
void demoLoadInitialMeshes(VKRT* vkrt, DemoGUIState* guiState);
