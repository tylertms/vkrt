#include "control_internal.h"
#include "debug.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t materialRequiresBackfaces(const MaterialData* material) {
    return (material && material->transmission > 0.0f) ? 1u : 0u;
}

VKRT_Result VKRT_uploadMeshData(
    VKRT* vkrt,
    const Vertex* vertices,
    size_t vertexCount,
    const uint32_t* indices,
    size_t indexCount
) {
    if (!vkrt || !vertices || !indices || vertexCount == 0 || indexCount == 0) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();
    vkDeviceWaitIdle(vkrt->core.device);

    if (vertexCount > UINT32_MAX || indexCount > UINT32_MAX) {
        LOG_ERROR("Mesh too large");
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    uint32_t previousCount = vkrt->core.meshData.count;
    uint32_t newCount = previousCount + 1;
    Mesh* resized = (Mesh*)realloc(vkrt->core.meshes, (size_t)newCount * sizeof(Mesh));
    if (!resized) {
        LOG_ERROR("Failed to grow mesh list");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    vkrt->core.meshes = resized;
    uint32_t newIndex = vkrt->core.meshData.count;
    Mesh* mesh = &vkrt->core.meshes[newIndex];
    memset(mesh, 0, sizeof(*mesh));

    uint32_t duplicateIndex = UINT32_MAX;
    for (uint32_t i = 0; i < vkrt->core.meshData.count; i++) {
        Mesh* existing = &vkrt->core.meshes[i];
        if (existing->info.vertexCount != (uint32_t)vertexCount || existing->info.indexCount != (uint32_t)indexCount) {
            continue;
        }
        if (memcmp(existing->vertices, vertices, vertexCount * sizeof(Vertex)) != 0) {
            continue;
        }
        if (memcmp(existing->indices, indices, indexCount * sizeof(uint32_t)) != 0) {
            continue;
        }
        duplicateIndex = existing->geometrySource;
        break;
    }

    if (duplicateIndex != UINT32_MAX) {
        Mesh* source = &vkrt->core.meshes[duplicateIndex];
        mesh->vertices = source->vertices;
        mesh->indices = source->indices;
        mesh->geometrySource = duplicateIndex;
        mesh->ownsGeometry = 0;
    } else {
        mesh->vertices = (Vertex*)malloc(vertexCount * sizeof(Vertex));
        mesh->indices = (uint32_t*)malloc(indexCount * sizeof(uint32_t));
        if (!mesh->vertices || !mesh->indices) {
            free(mesh->vertices);
            free(mesh->indices);
            mesh->vertices = NULL;
            mesh->indices = NULL;
            if (previousCount == 0) {
                free(vkrt->core.meshes);
                vkrt->core.meshes = NULL;
            } else {
                Mesh* shrunk = (Mesh*)realloc(vkrt->core.meshes, (size_t)previousCount * sizeof(Mesh));
                if (shrunk) vkrt->core.meshes = shrunk;
            }
            LOG_ERROR("Failed to allocate mesh host data");
            return VKRT_ERROR_OPERATION_FAILED;
        }

        memcpy(mesh->vertices, vertices, vertexCount * sizeof(Vertex));
        memcpy(mesh->indices, indices, indexCount * sizeof(uint32_t));

        mesh->geometrySource = newIndex;
        mesh->ownsGeometry = 1;
    }

    mesh->info.vertexCount = (uint32_t)vertexCount;
    mesh->info.indexCount = (uint32_t)indexCount;
    mesh->info.materialIndex = newIndex;
    mesh->info.padding = 0;

    mesh->material = VKRT_materialDataDisneyDefault();
    mesh->info.renderBackfaces = materialRequiresBackfaces(&mesh->material);

    vec3 scale = {1.f, 1.f, 1.f};
    memcpy(&mesh->info.scale, &scale, sizeof(vec3));
    memset(&mesh->info.rotation, 0, sizeof(vec3));
    memset(&mesh->info.position, 0, sizeof(vec3));

    vkrt->core.meshData.count = newCount;
    if (rebuildSceneGeometry(vkrt) != VKRT_SUCCESS) {
        if (mesh->ownsGeometry) {
            destroyMeshAccelerationStructure(vkrt, mesh);
            free(mesh->vertices);
            free(mesh->indices);
            mesh->vertices = NULL;
            mesh->indices = NULL;
        }

        vkrt->core.meshData.count = previousCount;
        if (previousCount == 0) {
            free(vkrt->core.meshes);
            vkrt->core.meshes = NULL;
        } else {
            Mesh* shrunk = (Mesh*)realloc(vkrt->core.meshes, (size_t)previousCount * sizeof(Mesh));
            if (shrunk) vkrt->core.meshes = shrunk;
        }

        if (rebuildSceneGeometry(vkrt) != VKRT_SUCCESS) {
            LOG_ERROR("Failed to restore scene geometry after mesh upload rollback");
        }
        return VKRT_ERROR_OPERATION_FAILED;
    }

    LOG_TRACE("Mesh upload complete. Total Meshes: %u, Vertices: %zu, Indices: %zu, Reused Geometry: %s, in %.3f ms",
        vkrt->core.meshData.count,
        vertexCount,
        indexCount,
        duplicateIndex == UINT32_MAX ? "No" : "Yes",
        (double)(getMicroseconds() - startTime) / 1e3);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_removeMesh(VKRT* vkrt, uint32_t meshIndex) {
    if (!vkrt || meshIndex >= vkrt->core.meshData.count) return VKRT_ERROR_INVALID_ARGUMENT;

    uint64_t startTime = getMicroseconds();
    vkDeviceWaitIdle(vkrt->core.device);
    uint32_t previousCount = vkrt->core.meshData.count;
    uint32_t previousSelectedMesh = vkrt->state.selectedMeshIndex;
    uint32_t last = previousCount - 1;

    Mesh* previousMeshes = (Mesh*)malloc((size_t)previousCount * sizeof(Mesh));
    if (!previousMeshes) {
        LOG_ERROR("Failed to snapshot mesh state before removal");
        return VKRT_ERROR_OPERATION_FAILED;
    }
    memcpy(previousMeshes, vkrt->core.meshes, (size_t)previousCount * sizeof(Mesh));

    Mesh removed = vkrt->core.meshes[meshIndex];
    int32_t promotedIndex = -1;

    if (removed.ownsGeometry) {
        for (uint32_t i = 0; i < previousCount; i++) {
            if (i == meshIndex) continue;

            Mesh* candidate = &vkrt->core.meshes[i];
            if (candidate->ownsGeometry || candidate->geometrySource != meshIndex) continue;

            promotedIndex = (int32_t)i;
            candidate->ownsGeometry = 1;
            candidate->geometrySource = i;
            break;
        }

        if (promotedIndex >= 0) {
            for (uint32_t i = 0; i < previousCount; i++) {
                Mesh* mesh = &vkrt->core.meshes[i];
                if (!mesh->ownsGeometry && mesh->geometrySource == meshIndex) {
                    mesh->geometrySource = (uint32_t)promotedIndex;
                }
            }
        }
    }

    if (meshIndex != last) {
        memmove(&vkrt->core.meshes[meshIndex],
            &vkrt->core.meshes[meshIndex + 1],
            (size_t)(last - meshIndex) * sizeof(Mesh));
    }

    for (uint32_t i = 0; i < last; i++) {
        Mesh* mesh = &vkrt->core.meshes[i];
        if (mesh->ownsGeometry) {
            mesh->geometrySource = i;
            continue;
        }

        if (mesh->geometrySource > meshIndex) {
            mesh->geometrySource--;
        }
    }

    vkrt->core.meshData.count = last;
    if (vkrt->state.selectedMeshIndex == meshIndex) {
        vkrt->state.selectedMeshIndex = UINT32_MAX;
    } else if (vkrt->state.selectedMeshIndex != UINT32_MAX && vkrt->state.selectedMeshIndex > meshIndex) {
        vkrt->state.selectedMeshIndex--;
    }

    if (rebuildSceneGeometry(vkrt) != VKRT_SUCCESS) {
        memcpy(vkrt->core.meshes, previousMeshes, (size_t)previousCount * sizeof(Mesh));
        vkrt->core.meshData.count = previousCount;
        vkrt->state.selectedMeshIndex = previousSelectedMesh;
        if (rebuildSceneGeometry(vkrt) != VKRT_SUCCESS) {
            LOG_ERROR("Failed to restore scene geometry after mesh removal rollback");
        }
        free(previousMeshes);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (removed.ownsGeometry) {
        if (promotedIndex < 0) {
            free(removed.vertices);
            free(removed.indices);
        }
        destroyMeshAccelerationStructure(vkrt, &removed);
    }

    if (last == 0) {
        free(vkrt->core.meshes);
        vkrt->core.meshes = NULL;
    } else {
        Mesh* shrunk = (Mesh*)realloc(vkrt->core.meshes, (size_t)last * sizeof(Mesh));
        if (shrunk) vkrt->core.meshes = shrunk;
    }

    free(previousMeshes);

    LOG_TRACE("Mesh removal complete. Removed Index: %u, Remaining Meshes: %u, in %.3f ms",
        meshIndex,
        vkrt->core.meshData.count,
        (double)(getMicroseconds() - startTime) / 1e3);
    return VKRT_SUCCESS;
}
