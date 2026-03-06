#pragma once

#include "vkrt.h"

typedef struct MeshImportEntry {
    char* name;
    Vertex* vertices;
    uint32_t* indices;
    size_t vertexCount;
    size_t indexCount;
    Material material;
    uint8_t renderBackfaces;
    vec3 position;
    vec3 rotation;
    vec3 scale;
} MeshImportEntry;

typedef struct MeshImportData {
    MeshImportEntry* entries;
    uint32_t count;
} MeshImportData;

int meshLoadFromFile(const char* filePath, MeshImportData* outImportData);
void meshReleaseImportData(MeshImportData* importData);
