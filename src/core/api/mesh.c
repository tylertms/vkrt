#include "../../../external/cglm/include/types.h"
#include "constants.h"
#include "numeric.h"
#include "scene.h"
#include "state.h"
#include "textures.h"
#include "types.h"
#include "vkrt_engine_types.h"
#include "vkrt_internal.h"
#include "vkrt_types.h"

#include <mat3.h>
#include <mat4.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vec3.h>

static float* accessMaterialTextureRotation(Material* material, uint32_t textureSlot) {
    if (!material) return NULL;

    switch (textureSlot) {
        case VKRT_MATERIAL_TEXTURE_SLOT_BASE_COLOR:
            return &material->textureRotations[0];
        case VKRT_MATERIAL_TEXTURE_SLOT_METALLIC_ROUGHNESS:
            return &material->textureRotations[1];
        case VKRT_MATERIAL_TEXTURE_SLOT_NORMAL:
            return &material->textureRotations[2];
        case VKRT_MATERIAL_TEXTURE_SLOT_EMISSIVE:
            return &material->textureRotations[3];
        default:
            return NULL;
    }
}

static uint32_t queryMaterialTextureTexcoordSet(uint32_t packedSets, uint32_t textureSlot) {
    return (packedSets >> materialTextureTexcoordSetShift(textureSlot)) & 0xffu;
}

static void assignMaterialTextureTexcoordSet(uint32_t* packedSets, uint32_t textureSlot, uint32_t texcoordSet) {
    if (!packedSets) return;

    uint32_t shift = materialTextureTexcoordSetShift(textureSlot);
    uint32_t mask = 0xffu << shift;
    *packedSets = (*packedSets & ~mask) | ((texcoordSet & 0xffu) << shift);
}

typedef struct MaterialTextureSlotState {
    uint32_t* textureIndex;
    uint32_t* textureWrap;
    uint32_t* textureTexcoordSets;
    float* textureTransform;
    float* textureRotation;
    uint32_t expectedColorSpace;
} MaterialTextureSlotState;

static void resetMaterialTextureSlot(uint32_t textureSlot, MaterialTextureSlotState state) {
    if (!state.textureIndex || !state.textureWrap || !state.textureTexcoordSets || !state.textureTransform ||
        !state.textureRotation) {
        return;
    }

    *state.textureWrap = 0u;
    assignMaterialTextureTexcoordSet(state.textureTexcoordSets, textureSlot, 0u);
    setIdentityMaterialTextureTransform(state.textureTransform);
    *state.textureRotation = 0.0f;
}

static void sanitizeMaterialTextureSlot(const VKRT* vkrt, uint32_t textureSlot, MaterialTextureSlotState state) {
    if (!state.textureIndex || !state.textureWrap || !state.textureTexcoordSets || !state.textureTransform ||
        !state.textureRotation) {
        return;
    }

    if (*state.textureIndex == VKRT_INVALID_INDEX) {
        resetMaterialTextureSlot(textureSlot, state);
        return;
    }

    const SceneTexture* texture = vkrtGetSceneTexture(vkrt, *state.textureIndex);
    int valid = 0;
    if (texture) {
        valid = state.expectedColorSpace == VKRT_TEXTURE_COLOR_SPACE_SRGB
                  ? (texture->colorSpace == VKRT_TEXTURE_COLOR_SPACE_SRGB ||
                     texture->colorSpace == VKRT_TEXTURE_COLOR_SPACE_LINEAR)
                  : texture->colorSpace == VKRT_TEXTURE_COLOR_SPACE_LINEAR;
    }
    if (!valid) {
        *state.textureIndex = VKRT_INVALID_INDEX;
        resetMaterialTextureSlot(textureSlot, state);
        return;
    }

    if (*state.textureWrap == 0u) {
        *state.textureWrap = VKRT_TEXTURE_WRAP_DEFAULT;
    }
    if (!isfinite(*state.textureRotation)) {
        *state.textureRotation = 0.0f;
    }
    if (queryMaterialTextureTexcoordSet(*state.textureTexcoordSets, textureSlot) > 1u) {
        assignMaterialTextureTexcoordSet(state.textureTexcoordSets, textureSlot, 0u);
    }
}

static Material sanitizeMaterial(const VKRT* vkrt, Material material) {
    for (int componentIndex = 0; componentIndex < 3; componentIndex++) {
        material.baseColor[componentIndex] = vkrtFiniteClampf(material.baseColor[componentIndex], 0.0f, 0.0f, 1.0f);
        material.emissionColor[componentIndex] =
            vkrtFiniteClampf(material.emissionColor[componentIndex], 0.0f, 0.0f, INFINITY);
        material.sheenTintWeight[componentIndex] =
            vkrtFiniteClampf(material.sheenTintWeight[componentIndex], 0.0f, 0.0f, 1.0f);
        material.attenuationColor[componentIndex] =
            vkrtFiniteClampf(material.attenuationColor[componentIndex], 1.0f, 0.0f, 1.0f);
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
    material.absorptionCoefficient =
        vkrtFiniteClampf(material.absorptionCoefficient, 0.0f, 0.0f, VKRT_MAX_ABSORPTION_COEFFICIENT);
    material.emissionLuminance = vkrtFiniteClampf(material.emissionLuminance, 0.0f, 0.0f, INFINITY);
    material.normalTextureScale = vkrtFiniteOrf(material.normalTextureScale, 1.0f);
    if (material.normalTextureScale < 0.0f) material.normalTextureScale = 0.0f;
    material.opacity = vkrtFiniteClampf(material.opacity, 1.0f, 0.0f, 1.0f);
    material.alphaCutoff = vkrtFiniteClampf(material.alphaCutoff, 0.5f, 0.0f, 1.0f);
    if (material.alphaMode != VKRT_MATERIAL_ALPHA_MODE_MASK && material.alphaMode != VKRT_MATERIAL_ALPHA_MODE_BLEND) {
        material.alphaMode = VKRT_MATERIAL_ALPHA_MODE_OPAQUE;
    }
    for (int componentIndex = 0; componentIndex < 3; componentIndex++) {
        material.eta[componentIndex] = vkrtFiniteClampf(material.eta[componentIndex], 0.0f, 0.0f, INFINITY);
        material.k[componentIndex] = vkrtFiniteClampf(material.k[componentIndex], 0.0f, 0.0f, INFINITY);
    }
    sanitizeMaterialTextureSlot(
        vkrt,
        VKRT_MATERIAL_TEXTURE_SLOT_BASE_COLOR,
        (MaterialTextureSlotState){
            .textureIndex = &material.baseColorTextureIndex,
            .textureWrap = &material.baseColorTextureWrap,
            .textureTexcoordSets = &material.textureTexcoordSets,
            .textureTransform = material.baseColorTextureTransform,
            .textureRotation = accessMaterialTextureRotation(&material, VKRT_MATERIAL_TEXTURE_SLOT_BASE_COLOR),
            .expectedColorSpace = VKRT_TEXTURE_COLOR_SPACE_SRGB,
        }
    );
    sanitizeMaterialTextureSlot(
        vkrt,
        VKRT_MATERIAL_TEXTURE_SLOT_METALLIC_ROUGHNESS,
        (MaterialTextureSlotState){
            .textureIndex = &material.metallicRoughnessTextureIndex,
            .textureWrap = &material.metallicRoughnessTextureWrap,
            .textureTexcoordSets = &material.textureTexcoordSets,
            .textureTransform = material.metallicRoughnessTextureTransform,
            .textureRotation = accessMaterialTextureRotation(&material, VKRT_MATERIAL_TEXTURE_SLOT_METALLIC_ROUGHNESS),
            .expectedColorSpace = VKRT_TEXTURE_COLOR_SPACE_LINEAR,
        }
    );
    sanitizeMaterialTextureSlot(
        vkrt,
        VKRT_MATERIAL_TEXTURE_SLOT_NORMAL,
        (MaterialTextureSlotState){
            .textureIndex = &material.normalTextureIndex,
            .textureWrap = &material.normalTextureWrap,
            .textureTexcoordSets = &material.textureTexcoordSets,
            .textureTransform = material.normalTextureTransform,
            .textureRotation = accessMaterialTextureRotation(&material, VKRT_MATERIAL_TEXTURE_SLOT_NORMAL),
            .expectedColorSpace = VKRT_TEXTURE_COLOR_SPACE_LINEAR,
        }
    );
    sanitizeMaterialTextureSlot(
        vkrt,
        VKRT_MATERIAL_TEXTURE_SLOT_EMISSIVE,
        (MaterialTextureSlotState){
            .textureIndex = &material.emissiveTextureIndex,
            .textureWrap = &material.emissiveTextureWrap,
            .textureTexcoordSets = &material.textureTexcoordSets,
            .textureTransform = material.emissiveTextureTransform,
            .textureRotation = accessMaterialTextureRotation(&material, VKRT_MATERIAL_TEXTURE_SLOT_EMISSIVE),
            .expectedColorSpace = VKRT_TEXTURE_COLOR_SPACE_SRGB,
        }
    );

    return material;
}

static int materialComponentEqual(const float* a, const float* b, size_t count) {
    if (!a || !b) return 0;
    return memcmp(a, b, count * sizeof(float)) == 0;
}

static int materialsEqual(const Material* a, const Material* b) {
    if (!a || !b) return 0;

    return materialComponentEqual(a->baseColor, b->baseColor, 3) && a->roughness == b->roughness &&
           materialComponentEqual(a->emissionColor, b->emissionColor, 3) &&
           a->emissionLuminance == b->emissionLuminance && materialComponentEqual(a->eta, b->eta, 3) &&
           a->metallic == b->metallic && materialComponentEqual(a->k, b->k, 3) && a->anisotropic == b->anisotropic &&
           a->specular == b->specular && a->specularTint == b->specularTint &&
           materialComponentEqual(a->sheenTintWeight, b->sheenTintWeight, 4) && a->clearcoat == b->clearcoat &&
           a->clearcoatGloss == b->clearcoatGloss && a->ior == b->ior && a->diffuseRoughness == b->diffuseRoughness &&
           a->transmission == b->transmission && a->subsurface == b->subsurface &&
           a->sheenRoughness == b->sheenRoughness && a->absorptionCoefficient == b->absorptionCoefficient &&
           materialComponentEqual(a->attenuationColor, b->attenuationColor, 3) &&
           a->normalTextureScale == b->normalTextureScale && a->baseColorTextureIndex == b->baseColorTextureIndex &&
           a->metallicRoughnessTextureIndex == b->metallicRoughnessTextureIndex &&
           a->normalTextureIndex == b->normalTextureIndex && a->emissiveTextureIndex == b->emissiveTextureIndex &&
           a->baseColorTextureWrap == b->baseColorTextureWrap &&
           a->metallicRoughnessTextureWrap == b->metallicRoughnessTextureWrap &&
           a->normalTextureWrap == b->normalTextureWrap && a->emissiveTextureWrap == b->emissiveTextureWrap &&
           a->opacity == b->opacity && a->alphaCutoff == b->alphaCutoff && a->alphaMode == b->alphaMode &&
           a->textureTexcoordSets == b->textureTexcoordSets &&
           materialComponentEqual(a->baseColorTextureTransform, b->baseColorTextureTransform, 4) &&
           materialComponentEqual(a->metallicRoughnessTextureTransform, b->metallicRoughnessTextureTransform, 4) &&
           materialComponentEqual(a->normalTextureTransform, b->normalTextureTransform, 4) &&
           materialComponentEqual(a->emissiveTextureTransform, b->emissiveTextureTransform, 4) &&
           materialComponentEqual(a->textureRotations, b->textureRotations, 4);
}

static int meshVectorFinite(const vec3 value) {
    return value && isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static int meshScaleValid(const vec3 scale) {
    if (!meshVectorFinite(scale)) return 0;
    return fabsf(scale[0]) >= 1e-6f && fabsf(scale[1]) >= 1e-6f && fabsf(scale[2]) >= 1e-6f;
}

static int meshTransformMatrixValid(mat4 transform) {
    if (!transform) return 0;

    mat4 transformCopy = GLM_MAT4_IDENTITY_INIT;
    memcpy(transformCopy, transform, sizeof(transformCopy));
    mat3 linear = GLM_MAT3_IDENTITY_INIT;
    glm_mat4_pick3(transformCopy, linear);
    float determinant = glm_mat3_det(linear);
    if (!isfinite(determinant) || fabsf(determinant) < 1e-8f) {
        return 0;
    }

    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            if (!isfinite(transform[column][row])) return 0;
        }
    }
    return 1;
}

static int updateMeshTransformMatrix(mat4 destination, mat4 source) {
    if (!destination || !source) return 0;

    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            if (destination[column][row] != source[column][row]) {
                memcpy(destination, source, sizeof(mat4));
                return 1;
            }
        }
    }
    return 0;
}

static int meshOpacityValid(float opacity) {
    return isfinite(opacity) && opacity >= 0.0f && opacity <= 1.0f;
}

static int selectionMaskUsesMesh(const VKRT* vkrt, uint32_t meshIndex) {
    return vkrt && vkrt->sceneSettings.selectionEnabled && meshIndex < vkrt->core.meshCount &&
           vkrt->sceneSettings.selectedMeshIndex == meshIndex;
}

static int selectionMaskUsesMaterial(const VKRT* vkrt, uint32_t materialIndex) {
    if (!vkrt || !vkrt->sceneSettings.selectionEnabled) return 0;

    uint32_t selectedMeshIndex = vkrt->sceneSettings.selectedMeshIndex;
    if (selectedMeshIndex >= vkrt->core.meshCount) return 0;
    return vkrt->core.meshes[selectedMeshIndex].info.materialIndex == materialIndex;
}

static void formatMaterialName(char outName[VKRT_NAME_LEN], const char* name, uint32_t materialIndex) {
    if (!outName) return;
    if (name && name[0]) {
        snprintf(outName, VKRT_NAME_LEN, "%s", name);
    } else {
        snprintf(outName, VKRT_NAME_LEN, "Material %u", materialIndex);
    }
}

static VKRT_Result removeMaterialAtIndex(VKRT* vkrt, uint32_t materialIndex) {
    if (!vkrt || materialIndex >= vkrt->core.materialCount) return VKRT_ERROR_INVALID_ARGUMENT;
    if (materialIndex == 0u) return VKRT_ERROR_OPERATION_FAILED;

    int selectionMaskAffected = selectionMaskUsesMaterial(vkrt, materialIndex);
    for (uint32_t meshIndex = 0; meshIndex < vkrt->core.meshCount; meshIndex++) {
        if (vkrt->core.meshes[meshIndex].info.materialIndex == materialIndex) {
            vkrt->core.meshes[meshIndex].info.materialIndex = 0u;
            vkrt->core.meshes[meshIndex].hasMaterialAssignment = 0;
        }
    }

    uint32_t materialCount = vkrt->core.materialCount;
    if (materialIndex + 1u < materialCount) {
        memmove(
            &vkrt->core.materials[materialIndex],
            &vkrt->core.materials[materialIndex + 1u],
            (size_t)(materialCount - materialIndex - 1u) * sizeof(SceneMaterial)
        );
    }

    uint32_t newCount = materialCount - 1u;
    if (newCount == 0u) {
        free(vkrt->core.materials);
        vkrt->core.materials = NULL;
    } else {
        SceneMaterial* shrunk = (SceneMaterial*)realloc(vkrt->core.materials, (size_t)newCount * sizeof(SceneMaterial));
        if (shrunk) {
            vkrt->core.materials = shrunk;
        }
    }
    vkrt->core.materialCount = newCount;

    for (uint32_t meshIndex = 0; meshIndex < vkrt->core.meshCount; meshIndex++) {
        if (vkrt->core.meshes[meshIndex].info.materialIndex > materialIndex) {
            vkrt->core.meshes[meshIndex].info.materialIndex--;
        }
    }

    vkrtMarkMaterialResourcesDirty(vkrt);
    vkrtMarkSceneResourcesDirty(vkrt);
    vkrtMarkLightResourcesDirty(vkrt);
    if (selectionMaskAffected) {
        markSelectionMaskDirty(vkrt);
    }
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

static uint32_t collectMaterialTextureIndices(const Material* material, uint32_t outTextureIndices[4]) {
    if (!material || !outTextureIndices) return 0u;

    uint32_t count = 0u;
    const uint32_t candidates[] = {
        material->baseColorTextureIndex,
        material->metallicRoughnessTextureIndex,
        material->normalTextureIndex,
        material->emissiveTextureIndex,
    };

    for (uint32_t i = 0; i < 4u; i++) {
        uint32_t textureIndex = candidates[i];
        if (textureIndex == VKRT_INVALID_INDEX) continue;

        int duplicate = 0;
        for (uint32_t existingIndex = 0; existingIndex < count; existingIndex++) {
            if (outTextureIndices[existingIndex] == textureIndex) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;

        uint32_t insertIndex = count;
        while (insertIndex > 0u && outTextureIndices[insertIndex - 1u] < textureIndex) {
            outTextureIndices[insertIndex] = outTextureIndices[insertIndex - 1u];
            insertIndex--;
        }
        outTextureIndices[insertIndex] = textureIndex;
        count++;
    }

    return count;
}

static VKRT_Result releaseTextureIndicesIfUnused(VKRT* vkrt, const uint32_t* textureIndices, uint32_t textureCount) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    for (uint32_t i = 0; i < textureCount; i++) {
        uint32_t textureIndex = textureIndices[i];
        if (textureIndex == VKRT_INVALID_INDEX || textureIndex >= vkrt->core.textureCount) continue;
        if (vkrtCountTextureUsers(vkrt, textureIndex) != 0u) continue;

        VKRT_Result result = vkrtSceneRemoveTexture(vkrt, textureIndex);
        if (result != VKRT_SUCCESS) return result;
    }

    return VKRT_SUCCESS;
}

VKRT_Result vkrtReleaseTextureIfUnused(VKRT* vkrt, uint32_t textureIndex) {
    if (!vkrt || textureIndex == VKRT_INVALID_INDEX) return VKRT_SUCCESS;

    uint32_t textureIndices[] = {textureIndex};
    return releaseTextureIndicesIfUnused(vkrt, textureIndices, 1u);
}

static VKRT_Result releaseTexturesReferencedByMaterialIfUnused(VKRT* vkrt, const Material* material) {
    uint32_t textureIndices[4] = {0};
    uint32_t textureCount = collectMaterialTextureIndices(material, textureIndices);
    if (textureCount == 0u) {
        return VKRT_SUCCESS;
    }

    return releaseTextureIndicesIfUnused(vkrt, textureIndices, textureCount);
}

VKRT_Result VKRT_getMeshCount(const VKRT* vkrt, uint32_t* outMeshCount) {
    if (!vkrt || !outMeshCount) return VKRT_ERROR_INVALID_ARGUMENT;
    *outMeshCount = vkrt->core.meshCount;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_addMaterial(VKRT* vkrt, const Material* material, const char* name, uint32_t* outMaterialIndex) {
    if (outMaterialIndex) *outMaterialIndex = VKRT_INVALID_INDEX;

    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;

    VKRT_Result defaultMaterialResult = vkrtEnsureDefaultMaterial(vkrt);
    if (defaultMaterialResult != VKRT_SUCCESS) return defaultMaterialResult;

    uint32_t materialIndex = vkrt->core.materialCount;
    SceneMaterial* resized =
        (SceneMaterial*)realloc(vkrt->core.materials, (size_t)(materialIndex + 1u) * sizeof(SceneMaterial));
    if (!resized) return VKRT_ERROR_OUT_OF_MEMORY;

    vkrt->core.materials = resized;
    vkrt->core.materials[materialIndex].material =
        sanitizeMaterial(vkrt, material ? *material : VKRT_materialDefault());
    formatMaterialName(vkrt->core.materials[materialIndex].name, name, materialIndex);
    vkrt->core.materialCount = materialIndex + 1u;
    vkrtAdjustMaterialTextureUseCounts(vkrt, &vkrt->core.materials[materialIndex].material, 1);
    vkrtMarkMaterialResourcesDirty(vkrt);
    resetSceneData(vkrt);

    if (outMaterialIndex) *outMaterialIndex = materialIndex;
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_removeMaterial(VKRT* vkrt, uint32_t materialIndex) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (materialIndex >= vkrt->core.materialCount) return VKRT_ERROR_INVALID_ARGUMENT;

    Material removedMaterial = vkrt->core.materials[materialIndex].material;
    vkrtAdjustMaterialTextureUseCounts(vkrt, &removedMaterial, -1);
    VKRT_Result result = removeMaterialAtIndex(vkrt, materialIndex);
    if (result != VKRT_SUCCESS) return result;
    return releaseTexturesReferencedByMaterialIfUnused(vkrt, &removedMaterial);
}

VKRT_Result VKRT_setMaterialName(VKRT* vkrt, uint32_t materialIndex, const char* name) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (!name || materialIndex >= vkrt->core.materialCount) return VKRT_ERROR_INVALID_ARGUMENT;

    SceneMaterial* material = &vkrt->core.materials[materialIndex];
    char formattedName[VKRT_NAME_LEN];
    formatMaterialName(formattedName, name, materialIndex);
    if (strncmp(material->name, formattedName, VKRT_NAME_LEN) == 0) {
        return VKRT_SUCCESS;
    }

    memcpy(material->name, formattedName, VKRT_NAME_LEN);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setMaterial(VKRT* vkrt, uint32_t materialIndex, const Material* material) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (!material || materialIndex >= vkrt->core.materialCount) return VKRT_ERROR_INVALID_ARGUMENT;

    Material sanitized = sanitizeMaterial(vkrt, *material);
    if (materialsEqual(&vkrt->core.materials[materialIndex].material, &sanitized)) return VKRT_SUCCESS;

    Material previousMaterial = vkrt->core.materials[materialIndex].material;
    vkrtAdjustMaterialTextureUseCounts(vkrt, &previousMaterial, -1);
    vkrt->core.materials[materialIndex].material = sanitized;
    vkrtAdjustMaterialTextureUseCounts(vkrt, &sanitized, 1);
    vkrtMarkMaterialResourcesDirty(vkrt);
    if (selectionMaskUsesMaterial(vkrt, materialIndex)) {
        markSelectionMaskDirty(vkrt);
    }
    resetSceneData(vkrt);
    return releaseTexturesReferencedByMaterialIfUnused(vkrt, &previousMaterial);
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

    Mesh* mesh = &vkrt->core.meshes[meshIndex];
    vec3 resolvedPosition = {0.0f, 0.0f, 0.0f};
    vec3 resolvedRotation = {0.0f, 0.0f, 0.0f};
    vec3 resolvedScale = {1.0f, 1.0f, 1.0f};
    if (position) {
        glm_vec3_copy(position, resolvedPosition);
    } else {
        glm_vec3_copy(mesh->info.position, resolvedPosition);
    }
    if (rotation) {
        glm_vec3_copy(rotation, resolvedRotation);
    } else {
        glm_vec3_copy(mesh->info.rotation, resolvedRotation);
    }
    if (scale) {
        glm_vec3_copy(scale, resolvedScale);
    } else {
        glm_vec3_copy(mesh->info.scale, resolvedScale);
    }

    mat4 worldTransform = GLM_MAT4_IDENTITY_INIT;
    buildMeshTransformMatrix(resolvedPosition, resolvedRotation, resolvedScale, worldTransform);
    return VKRT_setMeshTransformMatrix(vkrt, meshIndex, worldTransform);
}

VKRT_Result VKRT_setMeshTransformMatrix(VKRT* vkrt, uint32_t meshIndex, mat4 worldTransform) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (meshIndex >= vkrt->core.meshCount || !meshTransformMatrixValid(worldTransform)) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    Mesh* mesh = &vkrt->core.meshes[meshIndex];
    if (!updateMeshTransformMatrix(mesh->worldTransform, worldTransform)) {
        return VKRT_SUCCESS;
    }

    decomposeImportedMeshTransform(mesh->worldTransform, mesh->info.position, mesh->info.rotation, mesh->info.scale);
    vkrtMarkSceneResourcesDirty(vkrt);
    const Material* material = vkrtGetSceneMaterialData(vkrt, mesh->info.materialIndex);
    if (material && material->emissionLuminance > 0.0f) {
        vkrtMarkLightResourcesDirty(vkrt);
    }
    markSelectionMaskDirty(vkrt);
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setMeshMaterialIndex(VKRT* vkrt, uint32_t meshIndex, uint32_t materialIndex) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (meshIndex >= vkrt->core.meshCount || materialIndex >= vkrt->core.materialCount) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    Mesh* mesh = &vkrt->core.meshes[meshIndex];
    MeshInfo* info = &mesh->info;
    if (info->materialIndex == materialIndex && mesh->hasMaterialAssignment) return VKRT_SUCCESS;

    const Material* oldMaterial = vkrtGetSceneMaterialData(vkrt, info->materialIndex);
    const Material* newMaterial = vkrtGetSceneMaterialData(vkrt, materialIndex);
    int affectsLighting = (oldMaterial && oldMaterial->emissionLuminance > 0.0f) ||
                          (newMaterial && newMaterial->emissionLuminance > 0.0f);
    info->materialIndex = materialIndex;
    mesh->hasMaterialAssignment = 1u;
    vkrtMarkSceneResourcesDirty(vkrt);
    if (affectsLighting) {
        vkrtMarkLightResourcesDirty(vkrt);
    }
    if (selectionMaskUsesMesh(vkrt, meshIndex)) {
        markSelectionMaskDirty(vkrt);
    }
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_clearMeshMaterialAssignment(VKRT* vkrt, uint32_t meshIndex) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (meshIndex >= vkrt->core.meshCount) return VKRT_ERROR_INVALID_ARGUMENT;

    VKRT_Result defaultMaterialResult = vkrtEnsureDefaultMaterial(vkrt);
    if (defaultMaterialResult != VKRT_SUCCESS) return defaultMaterialResult;

    Mesh* mesh = &vkrt->core.meshes[meshIndex];
    MeshInfo* info = &mesh->info;
    if (!mesh->hasMaterialAssignment && info->materialIndex == 0u) {
        return VKRT_SUCCESS;
    }

    const Material* oldMaterial = vkrtGetSceneMaterialData(vkrt, info->materialIndex);
    const Material* defaultMaterial = vkrtGetSceneMaterialData(vkrt, 0u);
    int affectsLighting = (oldMaterial && oldMaterial->emissionLuminance > 0.0f) ||
                          (defaultMaterial && defaultMaterial->emissionLuminance > 0.0f);
    info->materialIndex = 0u;
    mesh->hasMaterialAssignment = 0u;
    vkrtMarkSceneResourcesDirty(vkrt);
    if (affectsLighting) {
        vkrtMarkLightResourcesDirty(vkrt);
    }
    if (selectionMaskUsesMesh(vkrt, meshIndex)) {
        markSelectionMaskDirty(vkrt);
    }
    resetSceneData(vkrt);
    return VKRT_SUCCESS;
}

VKRT_Result VKRT_setMeshOpacity(VKRT* vkrt, uint32_t meshIndex, float opacity) {
    VKRT_Result stateReady = vkrtRequireSceneStateReady(vkrt);
    if (stateReady != VKRT_SUCCESS) return stateReady;
    if (meshIndex >= vkrt->core.meshCount || !meshOpacityValid(opacity)) {
        return VKRT_ERROR_INVALID_ARGUMENT;
    }

    Mesh* mesh = &vkrt->core.meshes[meshIndex];
    MeshInfo* info = &mesh->info;
    if (info->opacity == opacity) {
        return VKRT_SUCCESS;
    }

    info->opacity = opacity;
    vkrtMarkSceneResourcesDirty(vkrt);
    const Material* material = vkrtGetSceneMaterialData(vkrt, info->materialIndex);
    if (material && material->emissionLuminance > 0.0f) {
        vkrtMarkLightResourcesDirty(vkrt);
    }
    markSelectionMaskDirty(vkrt);
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
