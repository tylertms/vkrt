#pragma once

#include <stdint.h>

typedef struct Session {
    char** meshNames;
    uint32_t meshCount;
    uint32_t pendingMeshRemovalIndex;
    char* pendingMeshImportPath;
} Session;

void sessionInit(Session* session);
void sessionDeinit(Session* session);

void sessionSetMeshName(Session* session, const char* filePath, uint32_t meshIndex);
void sessionRemoveMeshName(Session* session, uint32_t meshIndex);
const char* sessionGetMeshName(const Session* session, uint32_t meshIndex);

void sessionQueueMeshImport(Session* session, const char* path);
void sessionClearQueuedMeshImport(Session* session);
