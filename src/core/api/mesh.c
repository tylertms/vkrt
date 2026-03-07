#include "shared.h"
#include "scene.h"
#include "vkrt_internal.h"

#include <math.h>
#include <string.h>

static float clampFloatValue(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static Material sanitizeMaterial(Material material) {
    for (int c = 0; c < 3; c++) {
        material.baseColor[c] = clampFloatValue(material.baseColor[c], 0.0f, 1.0f);
        material.emissionColor[c] = fmaxf(material.emissionColor[c], 0.0f);
    }

    material.metallic = clampFloatValue(material.metallic, 0.0f, 1.0f);
    material.roughness = clampFloatValue(material.roughness, 0.0f, 1.0f);
    material.specular = clampFloatValue(material.specular, 0.0f, 1.0f);
    material.specularTint = clampFloatValue(material.specularTint, 0.0f, 1.0f);
    material.anisotropic = clampFloatValue(material.anisotropic, 0.0f, 1.0f);
    material.sheen = clampFloatValue(material.sheen, 0.0f, 1.0f);
    material.sheenTint = clampFloatValue(material.sheenTint, 0.0f, 1.0f);
    material.clearcoat = clampFloatValue(material.clearcoat, 0.0f, 1.0f);
    material.clearcoatGloss = clampFloatValue(material.clearcoatGloss, 0.0f, 1.0f);
    material.subsurface = clampFloatValue(material.subsurface, 0.0f, 1.0f);
    material.transmission = clampFloatValue(material.transmission, 0.0f, 1.0f);
    material.ior = fmaxf(material.ior, 1.0f);
    material.emissionLuminance = fmaxf(material.emissionLuminance, 0.0f);
    memset(material.padding0, 0, sizeof(material.padding0));

    return material;
}

static int updateMeshVector(vec3 destination, vec3 source) {
    if (!source) return 0;
    if (memcmp(destination, source, sizeof(vec3)) == 0) return 0;
    glm_vec3_copy(source, destination);
    return 1;
}

VKRT_Result VKRT_getMeshCount(const VKRT* vkrt, uint32_t* outMeshCount) {
    if (!vkrt || !outMeshCount) return VKRT_ERROR_INVALID_ARGUMENT;
    *outMeshCount = vkrt->core.meshCount;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setMeshTransform(VKRT* vkrt, uint32_t meshIndex, vec3 position, vec3 rotation, vec3 scale) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (meshIndex >= vkrt->core.meshCount) return VKRT_ERROR_INVALID_ARGUMENT;

    MeshInfo* info = &vkrt->core.meshes[meshIndex].info;
    int changed = 0;
    changed |= updateMeshVector(info->position, position);
    changed |= updateMeshVector(info->rotation, rotation);
    changed |= updateMeshVector(info->scale, scale);

    if (!changed) return VKRT_SUCCESS;

    vkrtMarkSceneResourcesDirty(vkrt);
    if (vkrt->core.meshes[meshIndex].material.emissionLuminance > 0.0f) {
        vkrtMarkLightResourcesDirty(vkrt);
    }
    markSelectionMaskDirty(vkrt);
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setMeshMaterial(VKRT* vkrt, uint32_t meshIndex, const Material* material) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (!material || meshIndex >= vkrt->core.meshCount) return VKRT_ERROR_INVALID_ARGUMENT;

    Mesh* mesh = &vkrt->core.meshes[meshIndex];
    Material sanitized = sanitizeMaterial(*material);
    if (memcmp(&mesh->material, &sanitized, sizeof(sanitized)) == 0) return VKRT_SUCCESS;
    mesh->material = sanitized;

    uint32_t nextBackfaces = vkrtResolveMeshRenderBackfaces(mesh);
    if (mesh->info.renderBackfaces != nextBackfaces) {
        mesh->info.renderBackfaces = nextBackfaces;
        vkrtMarkSceneResourcesDirty(vkrt);
    }

    vkrtMarkMaterialResourcesDirty(vkrt);
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setMeshRenderBackfaces(VKRT* vkrt, uint32_t meshIndex, uint32_t enabled) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (meshIndex >= vkrt->core.meshCount) return VKRT_ERROR_INVALID_ARGUMENT;

    Mesh* mesh = &vkrt->core.meshes[meshIndex];
    MeshInfo* info = &mesh->info;
    uint32_t normalized = enabled ? 1u : 0u;
    if (mesh->renderBackfacesOverride == (int8_t)normalized && info->renderBackfaces == normalized) {
        return VKRT_SUCCESS;
    }

    mesh->renderBackfacesOverride = (int8_t)normalized;
    info->renderBackfaces = vkrtResolveMeshRenderBackfaces(mesh);
    vkrtMarkSceneResourcesDirty(vkrt);
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}
