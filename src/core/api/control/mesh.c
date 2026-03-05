#include "scene.h"
#include "vkrt_internal.h"

#include <math.h>
#include <string.h>

static float clampf(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static MaterialData sanitizeDisneyMaterial(MaterialData material) {
    for (int c = 0; c < 3; c++) {
        material.baseColor[c] = clampf(material.baseColor[c], 0.0f, 1.0f);
        material.emissionColor[c] = fmaxf(material.emissionColor[c], 0.0f);
    }

    material.metallic = clampf(material.metallic, 0.0f, 1.0f);
    material.roughness = clampf(material.roughness, 0.0f, 1.0f);
    material.specular = clampf(material.specular, 0.0f, 1.0f);
    material.specularTint = clampf(material.specularTint, 0.0f, 1.0f);
    material.anisotropic = clampf(material.anisotropic, 0.0f, 1.0f);
    material.sheen = clampf(material.sheen, 0.0f, 1.0f);
    material.sheenTint = clampf(material.sheenTint, 0.0f, 1.0f);
    material.clearcoat = clampf(material.clearcoat, 0.0f, 1.0f);
    material.clearcoatGloss = clampf(material.clearcoatGloss, 0.0f, 1.0f);
    material.subsurface = clampf(material.subsurface, 0.0f, 1.0f);
    material.transmission = clampf(material.transmission, 0.0f, 1.0f);
    material.ior = fmaxf(material.ior, 1.0f);
    material.emissionLuminance = fmaxf(material.emissionLuminance, 0.0f);
    memset(material.padding0, 0, sizeof(material.padding0));

    return material;
}

static uint32_t materialRequiresBackfaces(const MaterialData* material) {
    return (material && material->transmission > 0.0f) ? 1u : 0u;
}

VKRT_Result VKRT_getMeshCount(const VKRT* vkrt, uint32_t* outMeshCount) {
    if (!vkrt || !outMeshCount) return VKRT_ERROR_INVALID_ARGUMENT;
    *outMeshCount = vkrt->core.meshData.count;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setMeshTransform(VKRT* vkrt, uint32_t meshIndex, vec3 position, vec3 rotation, vec3 scale) {
    if (!vkrt || meshIndex >= vkrt->core.meshData.count) return VKRT_ERROR_INVALID_ARGUMENT;

    MeshInfo* info = &vkrt->core.meshes[meshIndex].info;
    if (position) glm_vec3_copy(position, info->position);
    if (rotation) glm_vec3_copy(rotation, info->rotation);
    if (scale) glm_vec3_copy(scale, info->scale);

    vkrt->core.topLevelAccelerationStructure.needsRebuild = 1;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setMeshMaterial(VKRT* vkrt, uint32_t meshIndex, const MaterialData* material) {
    if (!vkrt || !material || meshIndex >= vkrt->core.meshData.count) return VKRT_ERROR_INVALID_ARGUMENT;

    Mesh* mesh = &vkrt->core.meshes[meshIndex];
    mesh->material = sanitizeDisneyMaterial(*material);

    uint32_t nextBackfaces = materialRequiresBackfaces(&mesh->material);
    if (mesh->info.renderBackfaces != nextBackfaces) {
        mesh->info.renderBackfaces = nextBackfaces;
        vkrt->core.topLevelAccelerationStructure.needsRebuild = 1;
    }

    vkrt->core.materialDataDirty = VK_TRUE;
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}
