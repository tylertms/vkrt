#ifndef VKRT_RT_MAIN_PAYLOAD_SLANG
#define VKRT_RT_MAIN_PAYLOAD_SLANG

#include "../scene/constants.slang"

struct Payload {
    uint didHit;
    uint instanceIndex;
    uint materialIndex;
    uint frontFace;
    float hitDistance;
    uint packedShadingNormal;
    uint packedGeometricNormal;
    float4 tangent;

    static Payload make() {
        Payload payload;
        payload.didHit = 0u;
        payload.instanceIndex = VKRT_INVALID_INDEX;
        payload.materialIndex = VKRT_INVALID_INDEX;
        payload.frontFace = 0u;
        payload.hitDistance = 0.0;
        payload.packedShadingNormal = 0u;
        payload.packedGeometricNormal = 0u;
        payload.tangent = float4(0.0);
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
    }

    [mutating]
    void setSurfaceHit(
        uint hitInstanceIndex, uint hitMaterialIndex, uint hitFrontFace, float distance,
        uint shadingNormal, uint geometricNormal, float4 hitTangent
    ) {
        didHit = 1u;
        instanceIndex = hitInstanceIndex;
        materialIndex = hitMaterialIndex;
        frontFace = hitFrontFace;
        hitDistance = distance;
        packedShadingNormal = shadingNormal;
        packedGeometricNormal = geometricNormal;
        tangent = hitTangent;
    }
};

#endif
