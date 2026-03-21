#ifndef VKRT_TRACE_ALPHA_SLANG
#define VKRT_TRACE_ALPHA_SLANG

#include "../scene/resources.slang"
#include "../geometry/surface.slang"
#include "../material/textures.slang"

bool alphaMayRejectHit(Material material, float meshOpacity) {
    return materialUsesAlphaMask(material) || materialUsesAlphaBlend(material, meshOpacity);
}

bool materialUsesAlphaMask(Material material) {
    return material.alphaMode == VKRT_MATERIAL_ALPHA_MODE_MASK;
}

bool materialUsesAlphaBlend(Material material, float meshOpacity) {
    return material.alphaMode == VKRT_MATERIAL_ALPHA_MODE_BLEND ||
        material.opacity < 0.999 ||
        meshOpacity < 0.999;
}

float evaluateMaterialTextureAlpha(Material material, float4 vertexColor, float2 texcoord0, float2 texcoord1) {
    if (material.alphaMode == VKRT_MATERIAL_ALPHA_MODE_OPAQUE) {
        return vertexColor.a;
    }

    return sampleMaterialTextureSlot(material, VKRT_TEXTURE_SLOT_BASE_COLOR, texcoord0, texcoord1).a * vertexColor.a;
}

float evaluateMaterialOpacity(Material material, float meshOpacity, float4 vertexColor, float2 texcoord0, float2 texcoord1) {
    return saturate(meshOpacity * material.opacity * evaluateMaterialTextureAlpha(material, vertexColor, texcoord0, texcoord1));
}

bool alphaPass(
    Material material,
    float meshOpacity,
    float4 vertexColor,
    float2 texcoord0,
    float2 texcoord1,
    inout uint rng
) {
    if (materialUsesAlphaMask(material)) {
        if (evaluateMaterialTextureAlpha(material, vertexColor, texcoord0, texcoord1) < material.alphaCutoff) {
            return false;
        }
        float maskOpacity = saturate(meshOpacity * material.opacity * vertexColor.a);
        if (maskOpacity >= 1.0) {
            return true;
        }
        if (maskOpacity <= 0.0) {
            return false;
        }
        return rand(rng) <= maskOpacity;
    }
    float opacity = evaluateMaterialOpacity(material, meshOpacity, vertexColor, texcoord0, texcoord1);
    if (!materialUsesAlphaBlend(material, meshOpacity)) {
        return true;
    }
    if (opacity <= 0.0) {
        return false;
    }
    if (opacity >= 1.0) {
        return true;
    }
    return rand(rng) <= opacity;
}

bool alphaHitAccepted(
    MeshInfo mesh,
    Material material,
    uint primitiveIndex,
    float2 barycentrics,
    inout uint rng
) {
    if (!alphaMayRejectHit(material, mesh.opacity)) {
        return true;
    }

    SurfaceAlphaAttributes surface = evaluateSurfaceAlphaAttributes(mesh, primitiveIndex, barycentrics);
    return alphaPass(material, mesh.opacity, surface.color, surface.texcoord0, surface.texcoord1, rng);
}

bool selectionAlphaHitAccepted(
    MeshInfo mesh,
    Material material,
    uint primitiveIndex,
    float2 barycentrics
) {
    if (!materialUsesAlphaMask(material)) {
        return true;
    }

    SurfaceAlphaAttributes surface = evaluateSurfaceAlphaAttributes(mesh, primitiveIndex, barycentrics);
    return evaluateMaterialTextureAlpha(material, surface.color, surface.texcoord0, surface.texcoord1) >= material.alphaCutoff;
}

#endif
