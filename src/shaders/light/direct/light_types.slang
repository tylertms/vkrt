#ifndef VKRT_LIGHT_DIRECT_LIGHT_TYPES_SLANG
#define VKRT_LIGHT_DIRECT_LIGHT_TYPES_SLANG

static const uint VKRT_LIGHT_FLAG_VALID = 1u;

struct LightSample {
    float3 position;
    float3 normal;
    float3 emission;
    float pdf;
    uint instanceIndex = VKRT_INVALID_INDEX;
    uint primitiveIndex = VKRT_INVALID_INDEX;
    uint flags = 0u;

    __init() {
        position = float3(0.0);
        normal = float3(0.0);
        emission = float3(0.0);
        pdf = 0.0;
        flags = 0u;
    }

    bool valid() {
        return (flags & VKRT_LIGHT_FLAG_VALID) != 0u;
    }
};

struct DirectLightSurfaceSample {
    LightSample light;
    float3 wi;
    float shadowDistance;
    float pdfSolidAngle;
    uint valid = 0u;
};

#endif
