#include "object.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    VKRT_uploadMeshData(vkrt, vertices, numVertices, indices, numIndices);

    free(vertices);
    free(indices);
    cgltf_free(data);
}
