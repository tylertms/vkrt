#ifndef VKRT_UTILITY_RAY_SLANG
#define VKRT_UTILITY_RAY_SLANG

#include "../core/resources.slang"

RayDesc makeRay(float3 origin, float3 direction, float tMin, float tMax) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = tMax;
    return ray;
}

RayDesc makePrimaryRay(int2 pixel, float2 jitter) {
    int2 viewportOrigin = int2(scene.viewportRect.xy);
    float2 viewportSize = float2(scene.viewportRect.zw);
    float2 viewportPixel = float2(pixel - viewportOrigin) + 0.5 + jitter;
    float2 uv = viewportPixel / viewportSize;
    float2 ndc = uv * 2.0 - 1.0;

    float4 viewDir = mul(scene.projInverse, float4(ndc.x, ndc.y, 1.0, 1.0));

    return makeRay(
        mul(scene.viewInverse, float4(0.0, 0.0, 0.0, 1.0)).xyz,
        normalize(mul(scene.viewInverse, float4(viewDir.xyz, 0.0)).xyz),
        0.001,
        10000.0);
}

float3 safeNormalize(float3 value) {
    float lenSq = dot(value, value);
    if (lenSq <= 1e-12) {
        return float3(0.0, 0.0, 1.0);
    }

    return value * rsqrt(lenSq);
}

#endif
