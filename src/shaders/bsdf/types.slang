#ifndef VKRT_BSDF_TYPES_SLANG
#define VKRT_BSDF_TYPES_SLANG

#include "../../shared/types.h"

struct BSDFSample {
    float3 wi;
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

struct SpectralBSDFSample {
    float3 wi;
    float4 weight;
    float4 techniquePdf;
    uint isTransmission;

    bool isUsable() {
        return any(weight > 0.0);
    }
};

#endif
