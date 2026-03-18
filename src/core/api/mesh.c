#include "state.h"
#include "scene.h"
#include "vkrt_internal.h"
#include "numeric.h"

#include <math.h>
#include <string.h>

static Material sanitizeMaterial(Material material) {
    for (int c = 0; c < 3; c++) {
        material.baseColor[c] = vkrtFiniteClampf(material.baseColor[c], 0.0f, 0.0f, 1.0f);
        material.emissionColor[c] = vkrtFiniteClampf(material.emissionColor[c], 0.0f, 0.0f, INFINITY);
        material.sheenTintWeight[c] = vkrtFiniteClampf(material.sheenTintWeight[c], 0.0f, 0.0f, 1.0f);
        material.attenuationColor[c] = vkrtFiniteClampf(material.attenuationColor[c], 1.0f, 0.0f, 1.0f);
    }

    material.metallic = vkrtFiniteClampf(material.metallic, 0.0f, 0.0f, 1.0f);
    material.roughness = vkrtFiniteClampf(material.roughness, 0.0f, 0.0f, 1.0f);
    material.diffuseRoughness = vkrtFiniteClampf(material.diffuseRoughness, 0.0f, 0.0f, 1.0f);
    material.specular = vkrtFiniteClampf(material.specular, 0.0f, 0.0f, 1.0f);
    material.specularTint = vkrtFiniteClampf(material.specularTint, 0.0f, 0.0f, 1.0f);
    material.anisotropic = vkrtFiniteClampf(material.anisotropic, 0.0f, 0.0f, 1.0f);
    material.sheenTintWeight[3] = vkrtFiniteClampf(material.sheenTintWeight[3], 0.0f, 0.0f, 1.0f);
    material.clearcoat = vkrtFiniteClampf(material.clearcoat, 0.0f, 0.0f, 1.0f);
    material.clearcoatGloss = vkrtFiniteClampf(material.clearcoatGloss, 0.0f, 0.0f, 1.0f);
    material.ior = vkrtFiniteClampf(material.ior, 1.0f, 1.0f, 4.0f);
    material.transmission = vkrtFiniteClampf(material.transmission, 0.0f, 0.0f, 1.0f);
    material.subsurface = vkrtFiniteClampf(material.subsurface, 0.0f, 0.0f, 1.0f);
    material.sheenRoughness = vkrtFiniteClampf(material.sheenRoughness, 0.0f, 0.0f, 1.0f);
    material.absorptionCoefficient = vkrtFiniteClampf(material.absorptionCoefficient, 0.0f, 0.0f, VKRT_MAX_ABSORPTION_COEFFICIENT);
    material.emissionLuminance = vkrtFiniteClampf(material.emissionLuminance, 0.0f, 0.0f, INFINITY);
    for (int c = 0; c < 3; c++) {
        material.eta[c] = vkrtFiniteClampf(material.eta[c], 0.0f, 0.0f, INFINITY);
        material.k[c] = vkrtFiniteClampf(material.k[c], 0.0f, 0.0f, INFINITY);
    }

    return material;
}

static int materialComponentEqual(const float* a, const float* b, size_t count) {
    if (!a || !b) return 0;
    return memcmp(a, b, count * sizeof(float)) == 0;
}

static int materialsEqual(const Material* a, const Material* b) {
    if (!a || !b) return 0;

    return materialComponentEqual(a->baseColor, b->baseColor, 3) &&
        a->roughness == b->roughness &&
        materialComponentEqual(a->emissionColor, b->emissionColor, 3) &&
        a->emissionLuminance == b->emissionLuminance &&
        materialComponentEqual(a->eta, b->eta, 3) &&
        a->metallic == b->metallic &&
        materialComponentEqual(a->k, b->k, 3) &&
        a->anisotropic == b->anisotropic &&
        a->specular == b->specular &&
        a->specularTint == b->specularTint &&
        materialComponentEqual(a->sheenTintWeight, b->sheenTintWeight, 4) &&
        a->clearcoat == b->clearcoat &&
        a->clearcoatGloss == b->clearcoatGloss &&
        a->ior == b->ior &&
        a->diffuseRoughness == b->diffuseRoughness &&
        a->transmission == b->transmission &&
        a->subsurface == b->subsurface &&
        a->sheenRoughness == b->sheenRoughness &&
        a->absorptionCoefficient == b->absorptionCoefficient &&
        materialComponentEqual(a->attenuationColor, b->attenuationColor, 3);
}

static int updateMeshVector(vec3 destination, vec3 source) {
    if (!source) return 0;
    if (memcmp(destination, source, sizeof(vec3)) == 0) return 0;
    glm_vec3_copy(source, destination);
    return 1;
}

static int meshVectorFinite(const vec3 value) {
    return value &&
        isfinite(value[0]) &&
        isfinite(value[1]) &&
        isfinite(value[2]);
}

static int meshScaleValid(const vec3 scale) {
    if (!meshVectorFinite(scale)) return 0;
    return fabsf(scale[0]) >= 1e-6f &&
        fabsf(scale[1]) >= 1e-6f &&
        fabsf(scale[2]) >= 1e-6f;
}

VKRT_Result VKRT_getMeshCount(const VKRT* vkrt, uint32_t* outMeshCount) {
    if (!vkrt || !outMeshCount) return VKRT_ERROR_INVALID_ARGUMENT;
    *outMeshCount = vkrt->core.meshCount;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setMeshName(VKRT* vkrt, uint32_t meshIndex, const char* name) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (!name || meshIndex >= vkrt->core.meshCount) return VKRT_ERROR_INVALID_ARGUMENT;

    Mesh* mesh = &vkrt->core.meshes[meshIndex];
    const char* sanitizedName = name[0] ? name : "(unknown)";
    if (strncmp(mesh->name, sanitizedName, sizeof(mesh->name)) == 0) return VKRT_SUCCESS;

    snprintf(mesh->name, sizeof(mesh->name), "%s", sanitizedName);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setMeshTransform(VKRT* vkrt, uint32_t meshIndex, vec3 position, vec3 rotation, vec3 scale) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (meshIndex >= vkrt->core.meshCount) return VKRT_ERROR_INVALID_ARGUMENT;
    if (position && !meshVectorFinite(position)) return VKRT_ERROR_INVALID_ARGUMENT;
    if (rotation && !meshVectorFinite(rotation)) return VKRT_ERROR_INVALID_ARGUMENT;
    if (scale && !meshScaleValid(scale)) return VKRT_ERROR_INVALID_ARGUMENT;

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
    if (materialsEqual(&mesh->material, &sanitized)) return VKRT_SUCCESS;
    mesh->material = sanitized;
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
