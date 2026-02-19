#pragma once

#include "editor_state.h"
#include "vkrt.h"

void sceneControllerImportMesh(VKRT* runtime, EditorState* state, const char* path);
void sceneControllerApplyPendingActions(VKRT* runtime, EditorState* state);
void sceneControllerLoadDefaultAssets(VKRT* runtime, EditorState* state);
