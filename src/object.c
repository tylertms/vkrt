#include "object.h"
#include "buffer.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

void loadObject(VKRT* vkrt, const char* filename) {
    cgltf_options options = {0};
    cgltf_data* data = NULL;

    cgltf_result res = cgltf_parse_file(&options, filename, &data);
    if (res != cgltf_result_success) {
        perror("ERROR: Failed to parse GLTF");
        exit(EXIT_FAILURE);
    }

    res = cgltf_load_buffers(&options, data, filename);
    if (res != cgltf_result_success) {
        cgltf_free(data);
        return;
    }

    size_t numVertices = 0;
    size_t numIndices = 0;

    for (size_t m = 0; m < data->meshes_count; m++) {
        cgltf_mesh* mesh = &data->meshes[m];
        for (size_t p = 0; p < mesh->primitives_count; p++) {
            cgltf_primitive* prim = &mesh->primitives[p];
            cgltf_accessor* posAccessor = NULL;
            for (size_t a = 0; a < prim->attributes_count; a++) {
                if (prim->attributes[a].type == cgltf_attribute_type_position) {
                    posAccessor = prim->attributes[a].data;
                    break;
                }
            }
            if (!posAccessor)
                continue;
            numVertices += posAccessor->count;
            numIndices += prim->indices ? prim->indices->count : 0;
        }
    }

    Vertex* vertices = malloc(numVertices * sizeof(Vertex));
    uint32_t* indices = malloc(numIndices * sizeof(uint32_t));

    size_t vertexBase = 0;
    size_t indexBase = 0;

    for (size_t m = 0; m < data->meshes_count; m++) {
        cgltf_mesh* mesh = &data->meshes[m];
        for (size_t p = 0; p < mesh->primitives_count; p++) {
            cgltf_primitive* prim = &mesh->primitives[p];
            cgltf_accessor* posAccessor = NULL;
            cgltf_accessor* normAccessor = NULL;
            for (size_t a = 0; a < prim->attributes_count; a++) {
                if (prim->attributes[a].type == cgltf_attribute_type_position)
                    posAccessor = prim->attributes[a].data;
                else if (prim->attributes[a].type == cgltf_attribute_type_normal)
                    normAccessor = prim->attributes[a].data;
            }
            if (!posAccessor || !normAccessor)
                continue;

            uint8_t* rawV = (uint8_t*)posAccessor->buffer_view->buffer->data;
            rawV += posAccessor->buffer_view->offset + posAccessor->offset;
            size_t vStride = posAccessor->stride;

            uint8_t* rawN = (uint8_t*)normAccessor->buffer_view->buffer->data;
            rawN += normAccessor->buffer_view->offset + normAccessor->offset;
            size_t nStride = normAccessor->stride;

            for (size_t i = 0; i < posAccessor->count; i++) {
                float* pSrc = (float*)(rawV + i * vStride);
                float* nSrc = (float*)(rawN + i * nStride);
                Vertex* V = vertices + vertexBase + i;
                V->position[0] = pSrc[0];
                V->position[1] = pSrc[1];
                V->position[2] = -pSrc[2];
                V->normal[0] = -nSrc[0];
                V->normal[1] = -nSrc[1];
                V->normal[2] = -nSrc[2];
            }

            if (prim->indices) {
                cgltf_accessor* idxAccessor = prim->indices;
                uint8_t* rawI = (uint8_t*)idxAccessor->buffer_view->buffer->data;
                rawI += idxAccessor->buffer_view->offset + idxAccessor->offset;
                size_t iStride = idxAccessor->stride;
                for (size_t i = 0; i < idxAccessor->count; i++) {
                    uint32_t raw = 0;
                    if (idxAccessor->component_type == cgltf_component_type_r_16) {
                        uint16_t v;
                        memcpy(&v, rawI + i * iStride, sizeof(uint16_t));
                        raw = v;
                    } else {
                        memcpy(&raw, rawI + i * iStride, sizeof(uint32_t));
                    }
                    indices[indexBase + i] = raw + (uint32_t)vertexBase;
                }
                indexBase += idxAccessor->count;
            }

            vertexBase += posAccessor->count;
        }
    }

    vkrt->vertexCount = (uint32_t)numVertices;
    vkrt->indexCount = (uint32_t)numIndices;

    vkrt->vertexBufferDeviceAddress = createBufferFromHostData(
        vkrt, vertices, numVertices * sizeof(Vertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        &vkrt->vertexBuffer,
        &vkrt->vertexBufferMemory);

    vkrt->indexBufferDeviceAddress = createBufferFromHostData(
        vkrt, indices, numIndices * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        &vkrt->indexBuffer,
        &vkrt->indexBufferMemory);

    free(vertices);
    free(indices);
    cgltf_free(data);
}

void createUniformBuffer(VKRT* vkrt) {
    VkDeviceSize uniformBufferSize = sizeof(SceneUniform);
    createBuffer(vkrt, uniformBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vkrt->uniformBuffer, &vkrt->uniformBufferMemory);
    vkMapMemory(vkrt->device, vkrt->uniformBufferMemory, 0, uniformBufferSize, 0, (void**)&vkrt->uniformBufferMapped);
    memset(vkrt->uniformBufferMapped, 0, uniformBufferSize);
}