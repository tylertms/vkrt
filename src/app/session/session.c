#include "session.h"

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

static void ensureMeshSlotCount(Session* session, uint32_t requiredCount) {
    if (!session || requiredCount <= session->meshCount) return;

    char** resized = (char**)realloc(session->meshNames, (size_t)requiredCount * sizeof(char*));
    if (!resized) {
        perror("[ERROR]: Failed to resize mesh label list");
        exit(EXIT_FAILURE);
    }

    for (uint32_t index = session->meshCount; index < requiredCount; index++) {
        resized[index] = NULL;
    }

    session->meshNames = resized;
    session->meshCount = requiredCount;
}

void sessionInit(Session* session) {
    if (!session) return;

    memset(session, 0, sizeof(*session));
    session->pendingMeshRemovalIndex = UINT32_MAX;
}

void sessionDeinit(Session* session) {
    if (!session) return;

    for (uint32_t index = 0; index < session->meshCount; index++) {
        free(session->meshNames[index]);
    }
    free(session->meshNames);

    session->meshNames = NULL;
    session->meshCount = 0;

    sessionClearQueuedMeshImport(session);
}

void sessionSetMeshName(Session* session, const char* filePath, uint32_t meshIndex) {
    if (!session) return;

    ensureMeshSlotCount(session, meshIndex + 1);
    free(session->meshNames[meshIndex]);
    session->meshNames[meshIndex] = stringDuplicate(fileBasename(filePath));
}

void sessionRemoveMeshName(Session* session, uint32_t meshIndex) {
    if (!session || meshIndex >= session->meshCount) return;

    free(session->meshNames[meshIndex]);
    for (uint32_t index = meshIndex; index + 1 < session->meshCount; index++) {
        session->meshNames[index] = session->meshNames[index + 1];
    }

    session->meshCount--;
    if (session->meshCount == 0) {
        free(session->meshNames);
        session->meshNames = NULL;
        return;
    }

    char** shrunk = (char**)realloc(session->meshNames, (size_t)session->meshCount * sizeof(char*));
    if (shrunk) session->meshNames = shrunk;
}

const char* sessionGetMeshName(const Session* session, uint32_t meshIndex) {
    if (!session || meshIndex >= session->meshCount || !session->meshNames[meshIndex]) {
        return "(unknown)";
    }

    return session->meshNames[meshIndex];
}

void sessionQueueMeshImport(Session* session, const char* path) {
    if (!session) return;

    sessionClearQueuedMeshImport(session);
    if (!path || !path[0]) return;

    session->pendingMeshImportPath = stringDuplicate(path);
}

void sessionClearQueuedMeshImport(Session* session) {
    if (!session || !session->pendingMeshImportPath) return;

    free(session->pendingMeshImportPath);
    session->pendingMeshImportPath = NULL;
}
