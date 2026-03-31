#ifndef VKRT_MATERIAL_TEXTURES_SLANG
#define VKRT_MATERIAL_TEXTURES_SLANG

#include "../bsdf/base.slang"
#include "../geometry/surface.slang"
#include "../scene/resources.slang"

static const uint VKRT_WRAP_CLAMP_TO_EDGE = VKRT_TEXTURE_WRAP_CLAMP_TO_EDGE;
static const uint VKRT_WRAP_MIRRORED_REPEAT = VKRT_TEXTURE_WRAP_MIRRORED_REPEAT;
static const uint VKRT_WRAP_REPEAT = VKRT_TEXTURE_WRAP_REPEAT;
static const uint VKRT_TEXTURE_SLOT_BASE_COLOR = VKRT_MATERIAL_TEXTURE_SLOT_BASE_COLOR;
static const uint VKRT_TEXTURE_SLOT_METALLIC_ROUGHNESS = VKRT_MATERIAL_TEXTURE_SLOT_METALLIC_ROUGHNESS;
static const uint VKRT_TEXTURE_SLOT_NORMAL = VKRT_MATERIAL_TEXTURE_SLOT_NORMAL;
static const uint VKRT_TEXTURE_SLOT_EMISSIVE = VKRT_MATERIAL_TEXTURE_SLOT_EMISSIVE;

uint textureWrapModeS(uint packedWrap) {
    return packedWrap & 0xffffu;
}

uint textureWrapModeT(uint packedWrap) {
    return (packedWrap >> 16u) & 0xffffu;
}

uint textureWrapVariant(uint wrapMode) {
    switch (wrapMode) {
        case VKRT_WRAP_CLAMP_TO_EDGE:
            return 1u;
        case VKRT_WRAP_MIRRORED_REPEAT:
            return 2u;
        case VKRT_WRAP_REPEAT:
        default:
            return 0u;
    }
}

uint textureSamplerVariant(uint packedWrap) {
    return textureWrapVariant(textureWrapModeS(packedWrap)) * 3u + textureWrapVariant(textureWrapModeT(packedWrap));
}

float materialTextureRotation(Material material, uint textureSlot) {
    return material.textureRotations[textureSlot];
}

float2 evaluateTextureUV(float2 uv, float4 transform, float rotation) {
    float2 scaled = uv * transform.xy;
    float sinTheta = sin(rotation);
    float cosTheta = cos(rotation);
    float2 rotated = float2(cosTheta * scaled.x - sinTheta * scaled.y, sinTheta * scaled.x + cosTheta * scaled.y);
    float2 transformed = rotated + transform.zw;
    return transformed;
}

uint materialTextureTexcoordSet(Material material, uint textureSlot) {
    return (material.textureTexcoordSets >> (textureSlot * 8u)) & 0xffu;
}

float2 selectMaterialTextureUV(SurfaceTextureData surface, uint texcoordSet) {
    return texcoordSet == 1u ? surface.texcoord1 : surface.texcoord0;
}

float4 sampleTexture(uint textureIndex, float2 uv, uint packedWrap) {
    uint resourceIndex = NonUniformResourceIndex(textureIndex);
    uint samplerIndex = NonUniformResourceIndex(textureSamplerVariant(packedWrap));
    return sceneTextures[resourceIndex].SampleLevel(textureSamplers[samplerIndex], uv, 0.0);
}

float4 sampleMaterialTexture(
    uint textureIndex,
    uint packedWrap,
    float4 transform,
    float rotation,
    uint texcoordSet,
    SurfaceTextureData surface,
    float4 fallbackValue
) {
    if (textureIndex == VKRT_INVALID_INDEX) {
        return fallbackValue;
    }

    float2 uv = selectMaterialTextureUV(surface, texcoordSet);
    return sampleTexture(textureIndex, evaluateTextureUV(uv, transform, rotation), packedWrap);
}

float4 sampleBaseColorTexture(Material material, SurfaceTextureData surface) {
    return sampleMaterialTexture(
        material.baseColorTextureIndex,
        material.baseColorTextureWrap,
        material.baseColorTextureTransform,
        materialTextureRotation(material, VKRT_TEXTURE_SLOT_BASE_COLOR),
        materialTextureTexcoordSet(material, VKRT_TEXTURE_SLOT_BASE_COLOR),
        surface,
        float4(1.0)
    );
}

float4 sampleMetallicRoughnessTexture(Material material, SurfaceTextureData surface) {
    return sampleMaterialTexture(
        material.metallicRoughnessTextureIndex,
        material.metallicRoughnessTextureWrap,
        material.metallicRoughnessTextureTransform,
        materialTextureRotation(material, VKRT_TEXTURE_SLOT_METALLIC_ROUGHNESS),
        materialTextureTexcoordSet(material, VKRT_TEXTURE_SLOT_METALLIC_ROUGHNESS),
        surface,
        float4(1.0)
    );
}

float4 sampleNormalTexture(Material material, SurfaceTextureData surface) {
    return sampleMaterialTexture(
        material.normalTextureIndex,
        material.normalTextureWrap,
        material.normalTextureTransform,
        materialTextureRotation(material, VKRT_TEXTURE_SLOT_NORMAL),
        materialTextureTexcoordSet(material, VKRT_TEXTURE_SLOT_NORMAL),
        surface,
        float4(0.5, 0.5, 1.0, 1.0)
    );
}

float4 sampleEmissiveTexture(Material material, SurfaceTextureData surface) {
    return sampleMaterialTexture(
        material.emissiveTextureIndex,
        material.emissiveTextureWrap,
        material.emissiveTextureTransform,
        materialTextureRotation(material, VKRT_TEXTURE_SLOT_EMISSIVE),
        materialTextureTexcoordSet(material, VKRT_TEXTURE_SLOT_EMISSIVE),
        surface,
        float4(1.0)
    );
}

void applySurfaceTextures(inout Material material, SurfaceTextureData surface) {
    float4 baseColorSample = sampleBaseColorTexture(material, surface);
    material.baseColor *= baseColorSample.rgb * surface.color.rgb;

    float4 metallicRoughnessSample = sampleMetallicRoughnessTexture(material, surface);
    material.roughness = saturate(material.roughness * metallicRoughnessSample.g);
    material.metallic = saturate(material.metallic * metallicRoughnessSample.b);

    float3 emission =
        material.emissionColor * material.emissionLuminance * sampleEmissiveTexture(material, surface).rgb;
    float emissionMax = maxComponent(emission);
    if (emissionMax > 0.0) {
        material.emissionColor = emission / emissionMax;
        material.emissionLuminance = emissionMax;
    } else {
        material.emissionColor = float3(1.0);
        material.emissionLuminance = 0.0;
    }
}

float3 applyNormalTexture(Material material, SurfaceTextureData surface, ShadingBasis basis) {
    if (material.normalTextureIndex == VKRT_INVALID_INDEX) {
        return basis.normal;
    }

    float3 normalSample = sampleNormalTexture(material, surface).xyz * 2.0 - 1.0;
    normalSample.xy *= material.normalTextureScale;
    normalSample = safeNormalize(normalSample);

    return safeNormalize(
        basis.tangent * normalSample.x + basis.bitangent * normalSample.y + basis.normal * normalSample.z
    );
}

#endif
