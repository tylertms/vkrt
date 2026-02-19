#include "mesh_asset_loader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ParsedMeshData {
    Vertex* vertices;
    uint32_t* indices;
    size_t vertexCount;
    size_t indexCount;
} ParsedMeshData;

static void gatherMeshSize(const cgltf_data* data, size_t* vertexCount, size_t* indexCount) {
    *vertexCount = 0;
    *indexCount = 0;

    for (size_t meshIndex = 0; meshIndex < data->meshes_count; meshIndex++) {
        cgltf_mesh* mesh = &data->meshes[meshIndex];
        for (size_t primitiveIndex = 0; primitiveIndex < mesh->primitives_count; primitiveIndex++) {
            cgltf_primitive* primitive = &mesh->primitives[primitiveIndex];
            if (primitive->type != cgltf_primitive_type_triangles || !primitive->indices) {
                continue;
            }

            cgltf_accessor* positionAccessor = NULL;
            cgltf_accessor* normalAccessor = NULL;
            for (size_t attributeIndex = 0; attributeIndex < primitive->attributes_count; attributeIndex++) {
                if (primitive->attributes[attributeIndex].type == cgltf_attribute_type_position) {
                    positionAccessor = primitive->attributes[attributeIndex].data;
                } else if (primitive->attributes[attributeIndex].type == cgltf_attribute_type_normal) {
                    normalAccessor = primitive->attributes[attributeIndex].data;
                }
            }

            if (!positionAccessor || !normalAccessor) continue;

            *vertexCount += positionAccessor->count;
            *indexCount += primitive->indices->count;
        }
    }
}

static uint32_t readIndexValue(const cgltf_accessor* accessor, const uint8_t* baseAddress, size_t index) {
    const uint8_t* location = baseAddress + index * accessor->stride;

    if (accessor->component_type == cgltf_component_type_r_16u) {
        uint16_t value = 0;
        memcpy(&value, location, sizeof(value));
        return (uint32_t)value;
    }

    if (accessor->component_type == cgltf_component_type_r_32u) {
        uint32_t value = 0;
        memcpy(&value, location, sizeof(value));
        return value;
    }

    fprintf(stderr, "ERROR: Unsupported index component type %u\n", accessor->component_type);
    exit(EXIT_FAILURE);
}

static ParsedMeshData parseMeshData(const cgltf_data* data) {
    ParsedMeshData parsed = {0};
    gatherMeshSize(data, &parsed.vertexCount, &parsed.indexCount);

    parsed.vertices = (Vertex*)malloc(parsed.vertexCount * sizeof(Vertex));
    parsed.indices = (uint32_t*)malloc(parsed.indexCount * sizeof(uint32_t));

    size_t vertexBase = 0;
    size_t indexBase = 0;

    for (size_t meshIndex = 0; meshIndex < data->meshes_count; meshIndex++) {
        cgltf_mesh* mesh = &data->meshes[meshIndex];
        for (size_t primitiveIndex = 0; primitiveIndex < mesh->primitives_count; primitiveIndex++) {
            cgltf_primitive* primitive = &mesh->primitives[primitiveIndex];
            if (primitive->type != cgltf_primitive_type_triangles || !primitive->indices) {
                continue;
            }

            cgltf_accessor* positionAccessor = NULL;
            cgltf_accessor* normalAccessor = NULL;
            for (size_t attributeIndex = 0; attributeIndex < primitive->attributes_count; attributeIndex++) {
                if (primitive->attributes[attributeIndex].type == cgltf_attribute_type_position) {
                    positionAccessor = primitive->attributes[attributeIndex].data;
                } else if (primitive->attributes[attributeIndex].type == cgltf_attribute_type_normal) {
                    normalAccessor = primitive->attributes[attributeIndex].data;
                }
            }
            if (!positionAccessor || !normalAccessor) continue;

            for (size_t vertexOffset = 0; vertexOffset < positionAccessor->count; vertexOffset++) {
                float position[3] = {0};
                float normal[3] = {0};
                cgltf_accessor_read_float(positionAccessor, vertexOffset, position, 3);
                cgltf_accessor_read_float(normalAccessor, vertexOffset, normal, 3);

                Vertex* destination = &parsed.vertices[vertexBase + vertexOffset];
                memcpy(destination->position, position, sizeof(position));
                memcpy(destination->normal, normal, sizeof(normal));
            }

            cgltf_accessor* indexAccessor = primitive->indices;
            const uint8_t* baseAddress =
                (uint8_t*)indexAccessor->buffer_view->buffer->data +
                indexAccessor->buffer_view->offset +
                indexAccessor->offset;

            for (size_t indexOffset = 0; indexOffset < indexAccessor->count; indexOffset++) {
                parsed.indices[indexBase + indexOffset] =
                    readIndexValue(indexAccessor, baseAddress, indexOffset) + (uint32_t)vertexBase;
            }

            indexBase += indexAccessor->count;
            vertexBase += positionAccessor->count;
        }
    }

    return parsed;
}

void meshAssetLoadFromFile(VKRT* runtime, const char* filePath) {
    cgltf_options options = {0};
    cgltf_data* data = NULL;

    if (cgltf_parse_file(&options, filePath, &data) != cgltf_result_success) {
        fprintf(stderr, "ERROR: Failed to parse GLTF '%s'\n", filePath);
        exit(EXIT_FAILURE);
    }

    if (cgltf_load_buffers(&options, data, filePath) != cgltf_result_success) {
        cgltf_free(data);
        fprintf(stderr, "ERROR: Failed to load buffers for '%s'\n", filePath);
        exit(EXIT_FAILURE);
    }

    ParsedMeshData parsed = parseMeshData(data);
    VKRT_uploadMeshData(runtime, parsed.vertices, parsed.vertexCount, parsed.indices, parsed.indexCount);

    free(parsed.vertices);
    free(parsed.indices);
    cgltf_free(data);
}
