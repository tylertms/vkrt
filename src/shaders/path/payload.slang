#ifndef VKRT_RT_MAIN_PAYLOAD_SLANG
#define VKRT_RT_MAIN_PAYLOAD_SLANG

#include "../scene/constants.slang"
#include "../scene/resources.slang"

static const uint VKRT_SHADOW_VISIBILITY_OCCLUDED = 0u;
static const uint VKRT_SHADOW_VISIBILITY_VISIBLE = 1u;
static const uint VKRT_SHADOW_VISIBILITY_UNSUPPORTED_TRANSMISSION = 2u;

struct Payload {
    uint didHit;
    uint rng;
    uint instanceIndex;
    uint materialIndex;
    uint frontFace;
    float hitDistance;
    uint packedShadingNormal;
    uint packedGeometricNormal;
    float4 tangent;
    float4 color;
    float2 texcoord0;
    float2 texcoord1;

    static Payload make(uint seed) {
        Payload payload;
        payload.didHit = 0u;
        payload.rng = seed;
        payload.instanceIndex = VKRT_INVALID_INDEX;
        payload.materialIndex = VKRT_INVALID_INDEX;
        payload.frontFace = 0u;
        payload.hitDistance = 0.0;
        payload.packedShadingNormal = 0u;
        payload.packedGeometricNormal = 0u;
        payload.tangent = float4(0.0);
        payload.color = float4(1.0);
        payload.texcoord0 = float2(0.0);
        payload.texcoord1 = float2(0.0);
        return payload;
    }

    [mutating]
    void setMiss() {
        didHit = 0u;
        instanceIndex = VKRT_INVALID_INDEX;
        materialIndex = VKRT_INVALID_INDEX;
        frontFace = 0u;
        hitDistance = 0.0;
        packedShadingNormal = 0u;
        packedGeometricNormal = 0u;
        tangent = float4(0.0);
        color = float4(1.0);
        texcoord0 = float2(0.0);
        texcoord1 = float2(0.0);
    }

    [mutating]
    void setSurfaceHit(
        uint hitInstanceIndex, uint hitMaterialIndex, uint hitFrontFace, float distance,
        uint shadingNormal, uint geometricNormal, float4 hitTangent, float4 hitColor, float2 hitTexcoord0, float2 hitTexcoord1
    ) {
        didHit = 1u;
        instanceIndex = hitInstanceIndex;
        materialIndex = hitMaterialIndex;
        frontFace = hitFrontFace;
        hitDistance = distance;
        packedShadingNormal = shadingNormal;
        packedGeometricNormal = geometricNormal;
        tangent = hitTangent;
        color = hitColor;
        texcoord0 = hitTexcoord0;
        texcoord1 = hitTexcoord1;
    }
};

struct ShadowPayload {
    uint visibility;
    uint rng;

    static ShadowPayload make(uint seed) {
        ShadowPayload payload;
        payload.visibility = VKRT_SHADOW_VISIBILITY_OCCLUDED;
        payload.rng = seed;
        return payload;
    }
};

#endif
