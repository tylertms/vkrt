#ifndef VKRT_BSDF_TYPES_SLANG
#define VKRT_BSDF_TYPES_SLANG

#include "../../shared/types.h"

struct BSDFInput {
    float3 normal;
    float4 tangent;
    float3 wo;
    uint frontFace;
    Material material;

    static BSDFInput make(float3 normal, float4 tangent, float3 wo, uint frontFace, Material material) {
        BSDFInput input;
        input.normal = normal;
        input.tangent = tangent;
        input.wo = wo;
        input.frontFace = frontFace;
        input.material = material;
        return input;
    }
};

struct BSDFSample {
    float3 wi;
    float3 value;
    float3 weight;
    float pdf;
    uint isTransmission;

    bool isUsable() {
        return pdf > 0.0 && any(weight > 0.0);
    }
};

struct BSDFEval {
    float3 value;
    float pdf;
};

#endif
