#ifndef VKRT_BSDF_DIELECTRIC_TYPES_SLANG
#define VKRT_BSDF_DIELECTRIC_TYPES_SLANG

struct DielectricSample {
    float3 wi;
    uint isTransmission;
};

#endif
