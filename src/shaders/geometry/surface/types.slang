#ifndef VKRT_GEOMETRY_SURFACE_TYPES_SLANG
#define VKRT_GEOMETRY_SURFACE_TYPES_SLANG

#include "../../../shared/types.h"

struct SurfaceTextureData {
    float4 color = float4(1.0);
    float2 texcoord0 = float2(0.0);
    float2 texcoord1 = float2(0.0);

    __init() {}

    __init(float4 color, float2 texcoord0, float2 texcoord1) {
        this.color = color;
        this.texcoord0 = texcoord0;
        this.texcoord1 = texcoord1;
    }
};

struct SurfaceShadingData {
    uint materialIndex = VKRT_INVALID_INDEX;
    uint frontFace = 0u;
    float3 shadingNormal = float3(0.0, 0.0, 1.0);
    float3 geometricNormal = float3(0.0, 0.0, 1.0);
    float4 tangent = float4(1.0, 0.0, 0.0, 1.0);
    SurfaceTextureData textureData = SurfaceTextureData();
};

#endif
