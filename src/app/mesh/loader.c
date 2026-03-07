#include "loader.h"

#include "debug.h"
#include "io.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void releaseImportEntry(MeshImportEntry* entry) {
    if (!entry) return;
    free(entry->name);
    free(entry->vertices);
    free(entry->indices);
    memset(entry, 0, sizeof(*entry));
}

void meshReleaseImportData(MeshImportData* importData) {
    if (!importData) return;

    for (uint32_t i = 0; i < importData->count; i++) {
        releaseImportEntry(&importData->entries[i]);
    }

    free(importData->entries);
    importData->entries = NULL;
    importData->count = 0;
}

static int appendImportEntry(MeshImportData* importData, MeshImportEntry* entry) {
    if (!importData || !entry) return -1;

    MeshImportEntry* resized = (MeshImportEntry*)realloc(importData->entries,
        (size_t)(importData->count + 1u) * sizeof(MeshImportEntry));
    if (!resized) {
        return -1;
    }

    importData->entries = resized;
    importData->entries[importData->count] = *entry;
    importData->count++;
    memset(entry, 0, sizeof(*entry));
    return 0;
}

static const cgltf_accessor* findAttributeAccessor(const cgltf_primitive* primitive, cgltf_attribute_type type) {
    if (!primitive) return NULL;

    for (cgltf_size i = 0; i < primitive->attributes_count; i++) {
        if (primitive->attributes[i].type == type) {
            return primitive->attributes[i].data;
        }
    }

    return NULL;
}

static void buildEntryName(
    char* outName,
    size_t outNameSize,
    const cgltf_node* node,
    const cgltf_mesh* mesh,
    cgltf_size primitiveIndex
) {
    if (!outName || outNameSize == 0) return;

    const char* baseName = "mesh";
    if (node && node->name && node->name[0]) {
        baseName = node->name;
    } else if (mesh && mesh->name && mesh->name[0]) {
        baseName = mesh->name;
    }

    if (mesh && mesh->primitives_count > 1) {
        snprintf(outName, outNameSize, "%s_%zu", baseName, primitiveIndex);
    } else {
        snprintf(outName, outNameSize, "%s", baseName);
    }
}

static float max3(float a, float b, float c) {
    float value = a;
    if (b > value) value = b;
    if (c > value) value = c;
    return value;
}

static Material buildMaterial(const cgltf_material* sourceMaterial) {
    Material material = VKRT_materialDefault();
    if (!sourceMaterial) {
        return material;
    }

    if (sourceMaterial->has_pbr_metallic_roughness) {
        material.baseColor[0] = sourceMaterial->pbr_metallic_roughness.base_color_factor[0];
        material.baseColor[1] = sourceMaterial->pbr_metallic_roughness.base_color_factor[1];
        material.baseColor[2] = sourceMaterial->pbr_metallic_roughness.base_color_factor[2];
        material.metallic = sourceMaterial->pbr_metallic_roughness.metallic_factor;
        material.roughness = sourceMaterial->pbr_metallic_roughness.roughness_factor;
    }

    if (sourceMaterial->has_specular) {
        material.specular = sourceMaterial->specular.specular_factor;
    }

    if (sourceMaterial->has_clearcoat) {
        material.clearcoat = sourceMaterial->clearcoat.clearcoat_factor;
        material.clearcoatGloss = 1.0f - sourceMaterial->clearcoat.clearcoat_roughness_factor;
    }

    if (sourceMaterial->has_sheen) {
        material.sheen = max3(sourceMaterial->sheen.sheen_color_factor[0],
            sourceMaterial->sheen.sheen_color_factor[1],
            sourceMaterial->sheen.sheen_color_factor[2]);
        material.sheenTint = material.sheen > 0.0f ? 1.0f : 0.0f;
    }

    if (sourceMaterial->has_transmission) {
        material.transmission = sourceMaterial->transmission.transmission_factor;
    }

    if (sourceMaterial->has_ior) {
        material.ior = sourceMaterial->ior.ior;
    }

    float emissiveScale = sourceMaterial->has_emissive_strength
        ? sourceMaterial->emissive_strength.emissive_strength
        : 1.0f;
    vec3 emissive = {
        sourceMaterial->emissive_factor[0] * emissiveScale,
        sourceMaterial->emissive_factor[1] * emissiveScale,
        sourceMaterial->emissive_factor[2] * emissiveScale,
    };
    float emissiveMax = max3(emissive[0], emissive[1], emissive[2]);
    if (emissiveMax > 0.0f) {
        material.emissionColor[0] = emissive[0] / emissiveMax;
        material.emissionColor[1] = emissive[1] / emissiveMax;
        material.emissionColor[2] = emissive[2] / emissiveMax;
        material.emissionLuminance = emissiveMax;
    } else {
        material.emissionColor[0] = 1.0f;
        material.emissionColor[1] = 1.0f;
        material.emissionColor[2] = 1.0f;
        material.emissionLuminance = 0.0f;
    }

    return material;
}

static void decomposeNodeTransform(const cgltf_node* node, vec3 outPosition, vec3 outRotation, vec3 outScale) {
    if (!node || !outPosition || !outRotation || !outScale) return;

    mat4 worldTransform = GLM_MAT4_IDENTITY_INIT;
    cgltf_float rawWorldTransform[16] = {0};
    cgltf_node_transform_world(node, rawWorldTransform);
    memcpy(worldTransform, rawWorldTransform, sizeof(worldTransform));

    vec4 translation = {0.0f, 0.0f, 0.0f, 1.0f};
    mat4 rotationMatrix = GLM_MAT4_IDENTITY_INIT;
    vec3 scale = {1.0f, 1.0f, 1.0f};
    glm_decompose(worldTransform, translation, rotationMatrix, scale);

    for (int axis = 0; axis < 3; axis++) {
        if (fabsf(scale[axis]) < 1e-6f) {
            scale[axis] = 1.0f;
        }
    }

    vec3 rotationRadians = {0.0f, 0.0f, 0.0f};
    glm_euler_angles(rotationMatrix, rotationRadians);

    outPosition[0] = translation[0];
    outPosition[1] = translation[1];
    outPosition[2] = translation[2];

    outRotation[0] = glm_deg(rotationRadians[0]) + 90.0f;
    outRotation[1] = glm_deg(rotationRadians[1]);
    outRotation[2] = glm_deg(rotationRadians[2]) + 90.0f;

    outScale[0] = scale[0];
    outScale[1] = -scale[1];
    outScale[2] = scale[2];
}

static void generateNormals(Vertex* vertices, size_t vertexCount, const uint32_t* indices, size_t indexCount) {
    if (!vertices || !indices || vertexCount == 0 || indexCount < 3) return;

    for (size_t vertexIndex = 0; vertexIndex < vertexCount; vertexIndex++) {
        vertices[vertexIndex].normal[0] = 0.0f;
        vertices[vertexIndex].normal[1] = 0.0f;
        vertices[vertexIndex].normal[2] = 0.0f;
        vertices[vertexIndex].normal[3] = 0.0f;
    }

    for (size_t indexOffset = 0; indexOffset + 2 < indexCount; indexOffset += 3) {
        uint32_t i0 = indices[indexOffset + 0];
        uint32_t i1 = indices[indexOffset + 1];
        uint32_t i2 = indices[indexOffset + 2];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;

        vec3 p0 = {
            vertices[i0].position[0],
            vertices[i0].position[1],
            vertices[i0].position[2],
        };
        vec3 p1 = {
            vertices[i1].position[0],
            vertices[i1].position[1],
            vertices[i1].position[2],
        };
        vec3 p2 = {
            vertices[i2].position[0],
            vertices[i2].position[1],
            vertices[i2].position[2],
        };

        vec3 edge01 = {0.0f, 0.0f, 0.0f};
        vec3 edge02 = {0.0f, 0.0f, 0.0f};
        vec3 faceNormal = {0.0f, 0.0f, 0.0f};
        glm_vec3_sub(p1, p0, edge01);
        glm_vec3_sub(p2, p0, edge02);
        glm_vec3_cross(edge01, edge02, faceNormal);
        if (glm_vec3_norm(faceNormal) <= 0.0f) continue;

        glm_vec3_add(vertices[i0].normal, faceNormal, vertices[i0].normal);
        glm_vec3_add(vertices[i1].normal, faceNormal, vertices[i1].normal);
        glm_vec3_add(vertices[i2].normal, faceNormal, vertices[i2].normal);
    }

    for (size_t vertexIndex = 0; vertexIndex < vertexCount; vertexIndex++) {
        vec3 normal = {
            vertices[vertexIndex].normal[0],
            vertices[vertexIndex].normal[1],
            vertices[vertexIndex].normal[2],
        };
        if (glm_vec3_norm(normal) > 0.0f) {
            glm_vec3_normalize(normal);
        } else {
            normal[2] = 1.0f;
        }

        vertices[vertexIndex].normal[0] = normal[0];
        vertices[vertexIndex].normal[1] = normal[1];
        vertices[vertexIndex].normal[2] = normal[2];
        vertices[vertexIndex].normal[3] = 0.0f;
    }
}

static int buildPrimitiveEntry(
    const cgltf_node* node,
    const cgltf_mesh* mesh,
    cgltf_size primitiveIndex,
    MeshImportEntry* outEntry
) {
    if (!node || !mesh || !outEntry || primitiveIndex >= mesh->primitives_count) return -1;

    const cgltf_primitive* primitive = &mesh->primitives[primitiveIndex];
    if (primitive->type != cgltf_primitive_type_triangles) return 0;

    const cgltf_accessor* positionAccessor = findAttributeAccessor(primitive, cgltf_attribute_type_position);
    const cgltf_accessor* normalAccessor = findAttributeAccessor(primitive, cgltf_attribute_type_normal);
    if (!positionAccessor || positionAccessor->count == 0) return 0;

    MeshImportEntry entry = {0};
    entry.vertexCount = (size_t)positionAccessor->count;
    entry.indexCount = primitive->indices
        ? (size_t)primitive->indices->count
        : (size_t)positionAccessor->count;
    entry.material = buildMaterial(primitive->material);
    entry.renderBackfaces = primitive->material && primitive->material->double_sided ? 1u : 0u;

    char generatedName[256] = {0};
    buildEntryName(generatedName, sizeof(generatedName), node, mesh, primitiveIndex);
    entry.name = stringDuplicate(generatedName);
    entry.vertices = (Vertex*)calloc(entry.vertexCount, sizeof(Vertex));
    entry.indices = (uint32_t*)malloc(entry.indexCount * sizeof(uint32_t));
    if (!entry.name || !entry.vertices || !entry.indices) {
        releaseImportEntry(&entry);
        return -1;
    }

    for (size_t vertexIndex = 0; vertexIndex < entry.vertexCount; vertexIndex++) {
        float position[3] = {0.0f, 0.0f, 0.0f};
        float normal[3] = {0.0f, 0.0f, 1.0f};

        if (!cgltf_accessor_read_float(positionAccessor, (cgltf_size)vertexIndex, position, 3)) {
            releaseImportEntry(&entry);
            return -1;
        }

        entry.vertices[vertexIndex].position[0] = position[0];
        entry.vertices[vertexIndex].position[1] = position[1];
        entry.vertices[vertexIndex].position[2] = position[2];
        entry.vertices[vertexIndex].position[3] = 1.0f;

        if (normalAccessor && cgltf_accessor_read_float(normalAccessor, (cgltf_size)vertexIndex, normal, 3)) {
            entry.vertices[vertexIndex].normal[0] = normal[0];
            entry.vertices[vertexIndex].normal[1] = normal[1];
            entry.vertices[vertexIndex].normal[2] = normal[2];
            entry.vertices[vertexIndex].normal[3] = 0.0f;
        }
    }

    if (primitive->indices) {
        for (size_t indexOffset = 0; indexOffset < entry.indexCount; indexOffset++) {
            cgltf_size indexValue = cgltf_accessor_read_index(primitive->indices, (cgltf_size)indexOffset);
            if (indexValue >= entry.vertexCount) {
                releaseImportEntry(&entry);
                return -1;
            }
            entry.indices[indexOffset] = (uint32_t)indexValue;
        }
    } else {
        for (size_t indexOffset = 0; indexOffset < entry.indexCount; indexOffset++) {
            entry.indices[indexOffset] = (uint32_t)indexOffset;
        }
    }

    if (!normalAccessor) {
        generateNormals(entry.vertices, entry.vertexCount, entry.indices, entry.indexCount);
    }

    decomposeNodeTransform(node, entry.position, entry.rotation, entry.scale);

    *outEntry = entry;
    return 1;
}

static int collectNodeEntries(const cgltf_node* node, MeshImportData* importData) {
    if (!node || !importData) return -1;

    if (node->mesh) {
        for (cgltf_size primitiveIndex = 0; primitiveIndex < node->mesh->primitives_count; primitiveIndex++) {
            MeshImportEntry entry = {0};
            int buildResult = buildPrimitiveEntry(node, node->mesh, primitiveIndex, &entry);
            if (buildResult < 0) {
                releaseImportEntry(&entry);
                return -1;
            }
            if (buildResult == 0) continue;
            if (appendImportEntry(importData, &entry) != 0) {
                releaseImportEntry(&entry);
                return -1;
            }
        }
    }

    for (cgltf_size childIndex = 0; childIndex < node->children_count; childIndex++) {
        if (collectNodeEntries(node->children[childIndex], importData) != 0) {
            return -1;
        }
    }

    return 0;
}

int meshLoadFromFile(const char* filePath, MeshImportData* outImportData) {
    if (!filePath || !filePath[0] || !outImportData) return -1;

    char resolvedPath[VKRT_PATH_MAX] = {0};
    if (resolveExistingPath(filePath, resolvedPath, sizeof(resolvedPath)) != 0) {
        LOG_ERROR("Mesh file not found: %s", filePath);
        return -1;
    }

    *outImportData = (MeshImportData){0};

    cgltf_options options = {0};
    cgltf_data* data = NULL;

    if (cgltf_parse_file(&options, resolvedPath, &data) != cgltf_result_success) {
        LOG_ERROR("Failed to parse GLTF '%s'", resolvedPath);
        return -1;
    }

    if (cgltf_load_buffers(&options, data, resolvedPath) != cgltf_result_success) {
        cgltf_free(data);
        LOG_ERROR("Failed to load buffers for '%s'", resolvedPath);
        return -1;
    }

    int result = 0;
    if (data->scene && data->scene->nodes_count > 0) {
        for (cgltf_size nodeIndex = 0; nodeIndex < data->scene->nodes_count; nodeIndex++) {
            if (collectNodeEntries(data->scene->nodes[nodeIndex], outImportData) != 0) {
                result = -1;
                break;
            }
        }
    } else {
        for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; nodeIndex++) {
            if (data->nodes[nodeIndex].parent) continue;
            if (collectNodeEntries(&data->nodes[nodeIndex], outImportData) != 0) {
                result = -1;
                break;
            }
        }
    }

    cgltf_free(data);

    if (result != 0) {
        meshReleaseImportData(outImportData);
        LOG_ERROR("Failed to extract mesh entries from '%s'", resolvedPath);
        return -1;
    }

    if (outImportData->count == 0) {
        meshReleaseImportData(outImportData);
        LOG_ERROR("No triangle mesh primitives were found in '%s'", resolvedPath);
        return -1;
    }

    return 0;
}
