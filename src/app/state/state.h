#pragma once

#include <stdint.h>

typedef struct State {
    char** meshNames;
    uint32_t meshCount;
    uint32_t pendingMeshRemovalIndex;
    char* pendingMeshImportPath;
} State;

void stateInit(State* state);
void stateDeinit(State* state);

void stateSetMeshName(State* state, const char* filePath, uint32_t meshIndex);
void stateRemoveMeshName(State* state, uint32_t meshIndex);
const char* stateGetMeshName(const State* state, uint32_t meshIndex);

void stateQueueMeshImport(State* state, const char* path);
void stateClearQueuedMeshImport(State* state);
