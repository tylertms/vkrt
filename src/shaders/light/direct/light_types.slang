#ifndef VKRT_LIGHT_DIRECT_LIGHT_TYPES_SLANG
#define VKRT_LIGHT_DIRECT_LIGHT_TYPES_SLANG

static const uint VKRT_LIGHT_FLAG_VALID = 1u;

struct LightSample {
    float3 position;
    float3 normal;
    float3 emission;
    float pdf;
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

struct DirectLightRgbResult {
    float3 radiance;
    uint flags = 1u;

    __init() {
        radiance = float3(0.0);
        flags = 1u;
    }

    bool neeSupported() {
        return (flags & 1u) != 0u;
    }

    [mutating] void setNeeUnsupported() { flags = 0u; }
};

struct DirectLightSpectralResult {
    float spectralRadiance = 0.0;
    uint flags = 1u;

    __init() {
        spectralRadiance = 0.0;
        flags = 1u;
    }

    bool neeSupported() {
        return (flags & 1u) != 0u;
    }

    [mutating] void setNeeUnsupported() { flags = 0u; }
};

struct DirectLightHeroResult {
    float4 spectralRadiance = float4(0.0);
    uint flags = 1u;

    __init() {
        spectralRadiance = float4(0.0);
        flags = 1u;
    }

    bool neeSupported() {
        return (flags & 1u) != 0u;
    }

    [mutating] void setNeeUnsupported() { flags = 0u; }
};

#endif
