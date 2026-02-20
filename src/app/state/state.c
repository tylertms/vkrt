#include "state.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* fileBasename(const char* path) {
    if (!path || !path[0]) return "(unknown)";

    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* base = path;

    if (slash && backslash) {
        base = slash > backslash ? slash + 1 : backslash + 1;
    } else if (slash) {
        base = slash + 1;
    } else if (backslash) {
        base = backslash + 1;
    }

    return base;
}

static char* stringDuplicate(const char* value) {
    size_t length = strlen(value);
    char* copy = (char*)malloc(length + 1);
    if (!copy) return NULL;

    memcpy(copy, value, length + 1);
    return copy;
}

static void ensureMeshSlotCount(EditorState* state, uint32_t requiredCount) {
    if (!state || requiredCount <= state->meshCount) return;

    char** resized = (char**)realloc(state->meshNames, (size_t)requiredCount * sizeof(char*));
    if (!resized) {
        perror("[ERROR]: Failed to resize mesh label list");
        exit(EXIT_FAILURE);
    }

    for (uint32_t index = state->meshCount; index < requiredCount; index++) {
        resized[index] = NULL;
    }

    state->meshNames = resized;
    state->meshCount = requiredCount;
}

void editorStateInit(EditorState* state) {
    if (!state) return;

    memset(state, 0, sizeof(*state));
    state->pendingMeshRemovalIndex = UINT32_MAX;
}

void editorStateDeinit(EditorState* state) {
    if (!state) return;

    for (uint32_t index = 0; index < state->meshCount; index++) {
        free(state->meshNames[index]);
    }
    free(state->meshNames);

    state->meshNames = NULL;
    state->meshCount = 0;

    editorStateClearQueuedMeshImport(state);
}

void editorStateSetMeshName(EditorState* state, const char* filePath, uint32_t meshIndex) {
    if (!state) return;

    ensureMeshSlotCount(state, meshIndex + 1);
    free(state->meshNames[meshIndex]);
    state->meshNames[meshIndex] = stringDuplicate(fileBasename(filePath));
}

void editorStateRemoveMeshName(EditorState* state, uint32_t meshIndex) {
    if (!state || meshIndex >= state->meshCount) return;

    free(state->meshNames[meshIndex]);
    for (uint32_t index = meshIndex; index + 1 < state->meshCount; index++) {
        state->meshNames[index] = state->meshNames[index + 1];
    }

    state->meshCount--;
    if (state->meshCount == 0) {
        free(state->meshNames);
        state->meshNames = NULL;
        return;
    }

    char** shrunk = (char**)realloc(state->meshNames, (size_t)state->meshCount * sizeof(char*));
    if (shrunk) state->meshNames = shrunk;
}

const char* editorStateGetMeshName(const EditorState* state, uint32_t meshIndex) {
    if (!state || meshIndex >= state->meshCount || !state->meshNames[meshIndex]) {
        return "(unknown)";
    }

    return state->meshNames[meshIndex];
}

void editorStateQueueMeshImport(EditorState* state, const char* path) {
    if (!state) return;

    editorStateClearQueuedMeshImport(state);
    if (!path || !path[0]) return;

    state->pendingMeshImportPath = stringDuplicate(path);
}

void editorStateClearQueuedMeshImport(EditorState* state) {
    if (!state || !state->pendingMeshImportPath) return;

    free(state->pendingMeshImportPath);
    state->pendingMeshImportPath = NULL;
}
