#include "object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

static void generateMeshName(VKRT* vkrt, uint32_t existingCount, const char* filename, char out[64]) {
    const char* base = filename;
    const char* slash = strrchr(filename, '/');
#ifdef _WIN32
    const char* bslash = strrchr(filename, '\\');
    if (bslash && (!slash || bslash > slash))
        slash = bslash;
#endif
    if (slash)
        base = slash + 1;
    const char* dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    if (len > 63)
        len = 63;
    memcpy(out, base, len);
    out[len] = '\0';

    char candidate[64];
    strcpy(candidate, out);
    int suffix = 1;
    while (1) {
        int collision = 0;
        for (uint32_t i = 0; i < existingCount; i++) {
            if (strcmp(vkrt->meshes[i].name, candidate) == 0) {
                collision = 1;
                break;
            }
        }
        if (!collision) {
            strncpy(out, candidate, 64);
            break;
        }
        snprintf(candidate, sizeof(candidate), "%.*s_%d", (int)len, out, suffix++);
        candidate[63] = '\0';
    }
}

void loadObject(VKRT* vkrt, const char* filename) {
    cgltf_options options = {0};
    cgltf_data* data = NULL;

    if (cgltf_parse_file(&options, filename, &data) != cgltf_result_success) {
        fprintf(stderr, "ERROR: Failed to parse GLTF '%s'\n", filename);
        exit(EXIT_FAILURE);
    }
    if (cgltf_load_buffers(&options, data, filename) != cgltf_result_success) {
        cgltf_free(data);
        fprintf(stderr, "ERROR: Failed to load buffers for '%s'\n", filename);
        exit(EXIT_FAILURE);
    }

    size_t numVertices = 0, numIndices = 0;
    for (size_t m = 0; m < data->meshes_count; m++) {
        cgltf_mesh* mesh = &data->meshes[m];
        for (size_t p = 0; p < mesh->primitives_count; p++) {
            cgltf_primitive* prim = &mesh->primitives[p];
            if (prim->type != cgltf_primitive_type_triangles || !prim->indices)
                continue;
            cgltf_accessor* posAcc = NULL;
            cgltf_accessor* normAcc = NULL;
            for (size_t a = 0; a < prim->attributes_count; a++) {
                if (prim->attributes[a].type == cgltf_attribute_type_position)
                    posAcc = prim->attributes[a].data;
                else if (prim->attributes[a].type == cgltf_attribute_type_normal)
                    normAcc = prim->attributes[a].data;
            }
            if (!posAcc || !normAcc)
                continue;
            numVertices += posAcc->count;
            numIndices += prim->indices->count;
        }
    }

    Vertex* vertices = malloc(numVertices * sizeof(Vertex));
    uint32_t* indices = malloc(numIndices * sizeof(uint32_t));
    size_t vertexBase = 0, indexBase = 0;

    for (size_t m = 0; m < data->meshes_count; m++) {
        cgltf_mesh* mesh = &data->meshes[m];
        for (size_t p = 0; p < mesh->primitives_count; p++) {
            cgltf_primitive* prim = &mesh->primitives[p];
            if (prim->type != cgltf_primitive_type_triangles || !prim->indices)
                continue;

            cgltf_accessor* posAcc = NULL;
            cgltf_accessor* normAcc = NULL;
            for (size_t a = 0; a < prim->attributes_count; a++) {
                if (prim->attributes[a].type == cgltf_attribute_type_position)
                    posAcc = prim->attributes[a].data;
                else if (prim->attributes[a].type == cgltf_attribute_type_normal)
                    normAcc = prim->attributes[a].data;
            }
            if (!posAcc || !normAcc)
                continue;

            for (size_t i = 0; i < posAcc->count; i++) {
                float pos[3], norm[3];
                cgltf_accessor_read_float(posAcc, i, pos, 3);
                cgltf_accessor_read_float(normAcc, i, norm, 3);

                Vertex* V = &vertices[vertexBase + i];
                memcpy(V->position, pos, sizeof(float) * 3);
                memcpy(V->normal, norm, sizeof(float) * 3);
            }

            cgltf_accessor* idxAcc = prim->indices;
            size_t idxCount = idxAcc->count;
            uint8_t* raw = (uint8_t*)idxAcc->buffer_view->buffer->data + idxAcc->buffer_view->offset + idxAcc->offset;
            size_t stride = idxAcc->stride;
            for (size_t i = 0; i < idxCount; i++) {
                uint32_t idxValue;
                if (idxAcc->component_type == cgltf_component_type_r_16u) {
                    uint16_t tmp;
                    memcpy(&tmp, raw + i * stride, sizeof(tmp));
                    idxValue = tmp;
                } else if (idxAcc->component_type == cgltf_component_type_r_32u) {
                    uint32_t tmp;
                    memcpy(&tmp, raw + i * stride, sizeof(tmp));
                    idxValue = tmp;
                } else {
                    fprintf(stderr,
                            "ERROR: Unsupported index component type %u\n",
                            idxAcc->component_type);
                    exit(EXIT_FAILURE);
                }
                indices[indexBase + i] = idxValue + (uint32_t)vertexBase;
            }

            indexBase += idxCount;
            vertexBase += posAcc->count;
        }
    }

    uint32_t meshIndex = vkrt->meshData.count++;
    vkrt->meshes = realloc(vkrt->meshes, vkrt->meshData.count * sizeof(Mesh));
    memset(&vkrt->meshes[meshIndex], 0, sizeof(Mesh));
    generateMeshName(vkrt, meshIndex, filename, vkrt->meshes[meshIndex].name);

    vkrt->meshData.host = realloc(vkrt->meshData.host, vkrt->meshData.count * vkrt->meshData.stride);
    MeshInfo* meshInfos = (MeshInfo*)vkrt->meshData.host;
    MeshInfo* meshInfo = &meshInfos[meshIndex];
    meshInfo->vertexCount = (uint32_t)numVertices;
    meshInfo->indexCount = (uint32_t)numIndices;
    memset(&meshInfo->position, 0, sizeof(vec3));
    memset(&meshInfo->rotation, 0, sizeof(vec3));
    vec3 scale = {1.f, 1.f, 1.f};
    memcpy(&meshInfo->scale, &scale, sizeof(vec3));

    size_t vOffset = (size_t)vkrt->vertexData.count * vkrt->vertexData.stride;
    vkrt->vertexData.host = realloc(vkrt->vertexData.host, vOffset + numVertices * vkrt->vertexData.stride);
    memcpy((uint8_t*)vkrt->vertexData.host + vOffset, vertices, numVertices * vkrt->vertexData.stride);
    vkrt->vertexData.count += (uint32_t)numVertices;

    size_t iOffset = (size_t)vkrt->indexData.count * vkrt->indexData.stride;
    vkrt->indexData.host = realloc(vkrt->indexData.host, iOffset + numIndices * vkrt->indexData.stride);
    memcpy((uint8_t*)vkrt->indexData.host + iOffset, indices, numIndices * vkrt->indexData.stride);
    vkrt->indexData.count += (uint32_t)numIndices;

    free(vertices);
    free(indices);
    cgltf_free(data);
}

static int get_exe_dir(char* out, size_t sz) {
#if defined(_WIN32)
    DWORD len = GetModuleFileNameA(NULL, out, (DWORD)sz);
    if (len == 0 || len == sz)
        return -1;

    while (len && out[len] != '\\')
        --len;
    out[len] = '\0';
    return 0;
#elif defined(__APPLE__)
    uint32_t len = (uint32_t)sz;
    if (_NSGetExecutablePath(out, &len) != 0)
        return -1;
    char* dir = strrchr(out, '/');
    if (!dir)
        return -1;
    *dir = '\0';
    return 0;
#else
    ssize_t len = readlink("/proc/self/exe", out, sz - 1);
    if (len <= 0)
        return -1;
    out[len] = '\0';
    while (len && out[len] != '/')
        --len;
    out[len] = '\0';
    return 0;
#endif
}

FILE* fopen_exe_relative(const char* relpath, const char* mode) {
    char buf[4096];
    if (get_exe_dir(buf, sizeof buf) < 0) {
        perror("ERROR: cannot get exe path");
        return NULL;
    }

    strncat_s(buf, sizeof buf, "/", _TRUNCATE);
    strncat_s(buf, sizeof buf, relpath, _TRUNCATE);

    FILE* file = NULL;
    if (fopen_s(&file, buf, mode) != 0) {
        return NULL;
    }

    return file;
}

const char* readFile(const char* filename, size_t* fileSize) {
    FILE* file = fopen_exe_relative(filename, "rb");
    if (!file) {
        perror("ERROR: Failed to open file");
        exit(EXIT_FAILURE);
    }
    fseek(file, 0, SEEK_END);
    *fileSize = (size_t)ftell(file);
    fseek(file, 0, SEEK_SET);

    char* buffer = malloc(*fileSize);
    if (!buffer) {
        perror("ERROR: Failed to allocate memory!");
        fclose(file);
        exit(EXIT_FAILURE);
    }
    fread(buffer, 1, *fileSize, file);
    fclose(file);
    return buffer;
}