#pragma once

#include <stdint.h>

typedef struct EditorState {
    char** meshNames;
    uint32_t meshCount;
    uint32_t pendingMeshRemovalIndex;
    char* pendingMeshImportPath;
} EditorState;

void editorStateInit(EditorState* state);
void editorStateDeinit(EditorState* state);

void editorStateSetMeshName(EditorState* state, const char* filePath, uint32_t meshIndex);
void editorStateRemoveMeshName(EditorState* state, uint32_t meshIndex);
const char* editorStateGetMeshName(const EditorState* state, uint32_t meshIndex);

void editorStateQueueMeshImport(EditorState* state, const char* path);
void editorStateClearQueuedMeshImport(EditorState* state);
