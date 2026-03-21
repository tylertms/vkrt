#ifndef VKRT_MATERIAL_TEXTURES_SLANG
#define VKRT_MATERIAL_TEXTURES_SLANG

#include "../scene/resources.slang"
#include "../bsdf/base.slang"

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
    return textureWrapVariant(textureWrapModeS(packedWrap)) * 3u +
        textureWrapVariant(textureWrapModeT(packedWrap));
}

struct MaterialTextureSampleInfo {
    uint index;
    uint packedWrap;
    float4 transform;
    float rotation;
    float4 fallbackValue;
};

float materialTextureRotation(Material material, uint textureSlot) {
    return material.textureRotations[textureSlot];
}

float2 evaluateTextureUV(float2 uv, float4 transform, float rotation) {
    float2 scaled = uv * transform.xy;
    float sinTheta = sin(rotation);
    float cosTheta = cos(rotation);
    float2 rotated = float2(
        cosTheta * scaled.x - sinTheta * scaled.y,
        sinTheta * scaled.x + cosTheta * scaled.y
    );
    float2 transformed = rotated + transform.zw;
    return transformed;
}

uint materialTextureTexcoordSet(Material material, uint textureSlot) {
    return (material.textureTexcoordSets >> (textureSlot * 8u)) & 0xffu;
}

float2 selectMaterialTextureUV(Material material, float2 texcoord0, float2 texcoord1, uint textureSlot) {
    return materialTextureTexcoordSet(material, textureSlot) == 1u ? texcoord1 : texcoord0;
}

float4 sampleTexture(uint textureIndex, float2 uv, uint packedWrap) {
    uint resourceIndex = NonUniformResourceIndex(textureIndex);
    uint samplerIndex = NonUniformResourceIndex(textureSamplerVariant(packedWrap));
    return sceneTextures[resourceIndex].SampleLevel(textureSamplers[samplerIndex], uv, 0.0);
}

MaterialTextureSampleInfo queryMaterialTextureSampleInfo(Material material, uint textureSlot) {
    MaterialTextureSampleInfo info;
    info.index = VKRT_INVALID_INDEX;
    info.packedWrap = VKRT_TEXTURE_WRAP_DEFAULT;
    info.transform = float4(1.0, 1.0, 0.0, 0.0);
    info.rotation = materialTextureRotation(material, textureSlot);
    info.fallbackValue = textureSlot == VKRT_TEXTURE_SLOT_NORMAL
        ? float4(0.5, 0.5, 1.0, 1.0)
        : float4(1.0);

    switch (textureSlot) {
        case VKRT_TEXTURE_SLOT_BASE_COLOR:
            info.index = material.baseColorTextureIndex;
            info.packedWrap = material.baseColorTextureWrap;
            info.transform = material.baseColorTextureTransform;
            break;
        case VKRT_TEXTURE_SLOT_METALLIC_ROUGHNESS:
            info.index = material.metallicRoughnessTextureIndex;
            info.packedWrap = material.metallicRoughnessTextureWrap;
            info.transform = material.metallicRoughnessTextureTransform;
            break;
        case VKRT_TEXTURE_SLOT_NORMAL:
            info.index = material.normalTextureIndex;
            info.packedWrap = material.normalTextureWrap;
            info.transform = material.normalTextureTransform;
            break;
        case VKRT_TEXTURE_SLOT_EMISSIVE:
            info.index = material.emissiveTextureIndex;
            info.packedWrap = material.emissiveTextureWrap;
            info.transform = material.emissiveTextureTransform;
            break;
        default:
            break;
    }
    return info;
}

float4 sampleMaterialTextureSlot(Material material, uint textureSlot, float2 texcoord0, float2 texcoord1) {
    MaterialTextureSampleInfo info = queryMaterialTextureSampleInfo(material, textureSlot);
    if (info.index == VKRT_INVALID_INDEX) {
        return info.fallbackValue;
    }

    float2 uv = selectMaterialTextureUV(material, texcoord0, texcoord1, textureSlot);
    return sampleTexture(
        info.index,
        evaluateTextureUV(uv, info.transform, info.rotation),
        info.packedWrap
    );
}

void applySurfaceTextures(inout Material material, float4 vertexColor, float2 texcoord0, float2 texcoord1) {
    float4 baseColorSample = sampleMaterialTextureSlot(material, VKRT_TEXTURE_SLOT_BASE_COLOR, texcoord0, texcoord1);
    material.baseColor *= baseColorSample.rgb * vertexColor.rgb;

    float4 metallicRoughnessSample = sampleMaterialTextureSlot(material, VKRT_TEXTURE_SLOT_METALLIC_ROUGHNESS, texcoord0, texcoord1);
    material.roughness = saturate(material.roughness * metallicRoughnessSample.g);
    material.metallic = saturate(material.metallic * metallicRoughnessSample.b);

    float3 emission = material.emissionColor * material.emissionLuminance *
        sampleMaterialTextureSlot(material, VKRT_TEXTURE_SLOT_EMISSIVE, texcoord0, texcoord1).rgb;
    float emissionMax = maxComponent(emission);
    if (emissionMax > 0.0) {
        material.emissionColor = emission / emissionMax;
        material.emissionLuminance = emissionMax;
    } else {
        material.emissionColor = float3(1.0);
        material.emissionLuminance = 0.0;
    }
}

float3 applyNormalTexture(Material material, float2 texcoord0, float2 texcoord1, ShadingBasis basis) {
    if (material.normalTextureIndex == VKRT_INVALID_INDEX) {
        return basis.normal;
    }

    float3 normalSample = sampleMaterialTextureSlot(material, VKRT_TEXTURE_SLOT_NORMAL, texcoord0, texcoord1).xyz * 2.0 - 1.0;
    normalSample.xy *= material.normalTextureScale;
    normalSample = safeNormalize(normalSample);

    return safeNormalize(
        basis.tangent * normalSample.x +
        basis.bitangent * normalSample.y +
        basis.normal * normalSample.z
    );
}

#endif
