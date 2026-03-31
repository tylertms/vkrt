#ifndef VKRT_RT_ALPHA_TEST_SLANG
#define VKRT_RT_ALPHA_TEST_SLANG

#include "../geometry/surface/interpolation.slang"
#include "../material/textures.slang"
#include "../scene/resources.slang"

bool alphaMayRejectHit(Material material, float meshOpacity) {
    return materialUsesAlphaMask(material) || materialUsesAlphaBlend(material, meshOpacity);
}

bool materialUsesAlphaMask(Material material) {
    return material.alphaMode == VKRT_MATERIAL_ALPHA_MODE_MASK;
}

bool materialUsesAlphaBlend(Material material, float meshOpacity) {
    return material.alphaMode == VKRT_MATERIAL_ALPHA_MODE_BLEND || material.opacity < 0.999 || meshOpacity < 0.999;
}

float evaluateMaterialTextureAlpha(Material material, SurfaceTextureData surface) {
    if (material.alphaMode == VKRT_MATERIAL_ALPHA_MODE_OPAQUE) {
        return surface.color.a;
    }

    return sampleBaseColorTexture(material, surface).a * surface.color.a;
}

float evaluateMaterialOpacity(Material material, float meshOpacity, SurfaceTextureData surface) {
    return saturate(meshOpacity * material.opacity * evaluateMaterialTextureAlpha(material, surface));
}

bool alphaPass(Material material, float meshOpacity, SurfaceTextureData surface, inout uint rng) {
    if (materialUsesAlphaMask(material)) {
        if (evaluateMaterialTextureAlpha(material, surface) < material.alphaCutoff) {
            return false;
        }
        float maskOpacity = saturate(meshOpacity * material.opacity * surface.color.a);
        if (maskOpacity >= 1.0) {
            return true;
        }
        if (maskOpacity <= 0.0) {
            return false;
        }
        return rand(rng) <= maskOpacity;
    }
    float opacity = evaluateMaterialOpacity(material, meshOpacity, surface);
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

bool alphaHitAccepted(MeshInfo mesh, Material material, uint primitiveIndex, float2 barycentrics, inout uint rng) {
    if (!alphaMayRejectHit(material, mesh.opacity)) {
        return true;
    }

    SurfaceTextureData surface = evaluateSurfaceTextureData(mesh, primitiveIndex, barycentrics);
    return alphaPass(material, mesh.opacity, surface, rng);
}

bool selectionAlphaHitAccepted(MeshInfo mesh, Material material, uint primitiveIndex, float2 barycentrics) {
    if (!materialUsesAlphaMask(material)) {
        return true;
    }

    SurfaceTextureData surface = evaluateSurfaceTextureData(mesh, primitiveIndex, barycentrics);
    return evaluateMaterialTextureAlpha(material, surface) >= material.alphaCutoff;
}

#endif
