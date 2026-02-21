#pragma once

#include "state.h"
#include "vkrt.h"

void sceneControllerImportMesh(VKRT* runtime, State* state, const char* path);
void sceneControllerApplyPendingActions(VKRT* runtime, State* state);
void sceneControllerLoadDefaultAssets(VKRT* runtime, State* state);
