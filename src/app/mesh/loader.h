#pragma once

#include "vkrt.h"

typedef struct MaterialImportEntry {
    char* name;
    Material material;
} MaterialImportEntry;

typedef struct TextureImportEntry {
    char* name;
    void* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t colorSpace;
} TextureImportEntry;

typedef struct MeshImportEntry {
    char* name;
    Vertex* vertices;
    uint32_t* indices;
    size_t vertexCount;
    size_t indexCount;
    uint32_t nodeIndex;
    uint32_t materialIndex;
    uint8_t renderBackfaces;
    vec3 position;
    vec3 rotation;
    vec3 scale;
} MeshImportEntry;

typedef struct NodeImportEntry {
    mat4 localTransform;
    char* name;
    uint32_t parentIndex;
    uint32_t meshEntryCount;
    vec3 position;
    vec3 rotation;
    vec3 scale;
} NodeImportEntry;

typedef struct MeshImportData {
    MeshImportEntry* entries;
    uint32_t count;
    uint32_t entryCapacity;
    NodeImportEntry* nodes;
    uint32_t nodeCount;
    uint32_t nodeCapacity;
    MaterialImportEntry* materials;
    uint32_t materialCount;
    TextureImportEntry* textures;
    uint32_t textureCount;
    uint32_t textureCapacity;
} MeshImportData;

int meshLoadFromFile(const char* filePath, MeshImportData* outImportData);
void meshReleaseImportData(MeshImportData* importData);
