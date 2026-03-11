#ifndef VKRT_BSDF_COMMON_SLANG
#define VKRT_BSDF_COMMON_SLANG

#include "types.slang"

static const float VKRT_PI = 3.14159265358979323846;
static const float VKRT_INV_PI = 0.31830988618379067154;

float3 safeNormalize(float3 value) {
    float lenSq = dot(value, value);
    if (lenSq <= 1e-12) {
        return float3(0.0, 0.0, 1.0);
    }

    return value * rsqrt(lenSq);
}

void shadingBasis(float3 normal, out float3 tangent, out float3 bitangent) {
    float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    tangent = safeNormalize(cross(up, normal));
    bitangent = cross(normal, tangent);
}

float3 localToWorld(float3 localDir, float3 normal) {
    float3 tangent = float3(0.0);
    float3 bitangent = float3(0.0);
    shadingBasis(normal, tangent, bitangent);
    return tangent * localDir.x + bitangent * localDir.y + normal * localDir.z;
}

#endif
