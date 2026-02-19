#include "demo_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const char* basenameFromPath(const char* path) {
    if (!path || !path[0]) return "(unknown)";

    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* base = path;

    if (slash && backslash) base = (slash > backslash ? slash + 1 : backslash + 1);
    else if (slash) base = slash + 1;
    else if (backslash) base = backslash + 1;

    return base;
}

static char* copyString(const char* text) {
    size_t len = strlen(text);
    char* out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, text, len + 1);
    return out;
}

static void ensureMeshLabelCapacity(DemoGUIState* state, uint32_t requiredCount) {
    if (!state || requiredCount <= state->meshLabelCount) return;

    char** resized = (char**)realloc(state->meshLabels, (size_t)requiredCount * sizeof(char*));
    if (!resized) {
        perror("ERROR: Failed to grow mesh label list");
        exit(EXIT_FAILURE);
    }

    for (uint32_t i = state->meshLabelCount; i < requiredCount; i++) {
        resized[i] = NULL;
    }

    state->meshLabels = resized;
    state->meshLabelCount = requiredCount;
}

void initDemoGUIState(DemoGUIState* state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->pendingRemoveIndex = UINT32_MAX;
}

void deinitDemoGUIState(DemoGUIState* state) {
    if (!state) return;

    for (uint32_t i = 0; i < state->meshLabelCount; i++) {
        free(state->meshLabels[i]);
    }
    free(state->meshLabels);

    state->meshLabels = NULL;
    state->meshLabelCount = 0;

    demoGUIClearPendingAddPath(state);
}

void demoGUIOnMeshAdded(DemoGUIState* state, const char* filename, uint32_t meshIndex) {
    if (!state) return;

    ensureMeshLabelCapacity(state, meshIndex + 1);

    free(state->meshLabels[meshIndex]);
    state->meshLabels[meshIndex] = copyString(basenameFromPath(filename));
}

void demoGUIOnMeshRemoved(DemoGUIState* state, uint32_t removedIndex) {
    if (!state || removedIndex >= state->meshLabelCount) return;

    free(state->meshLabels[removedIndex]);

    for (uint32_t i = removedIndex; i + 1 < state->meshLabelCount; i++) {
        state->meshLabels[i] = state->meshLabels[i + 1];
    }

    state->meshLabelCount--;
    if (state->meshLabelCount == 0) {
        free(state->meshLabels);
        state->meshLabels = NULL;
        return;
    }

    char** shrunk = (char**)realloc(state->meshLabels, (size_t)state->meshLabelCount * sizeof(char*));
    if (shrunk) state->meshLabels = shrunk;
}

void demoGUIQueueAddMeshPath(DemoGUIState* state, const char* path) {
    if (!state) return;

    demoGUIClearPendingAddPath(state);
    if (!path || !path[0]) return;

    state->pendingAddPath = copyString(path);
}

void demoGUIClearPendingAddPath(DemoGUIState* state) {
    if (!state || !state->pendingAddPath) return;
    free(state->pendingAddPath);
    state->pendingAddPath = NULL;
}

const char* demoGUIGetMeshLabel(const DemoGUIState* state, uint32_t meshIndex) {
    if (!state || meshIndex >= state->meshLabelCount || !state->meshLabels[meshIndex]) {
        return "(unknown)";
    }
    return state->meshLabels[meshIndex];
}
