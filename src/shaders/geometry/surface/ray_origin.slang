#ifndef VKRT_GEOMETRY_SURFACE_RAY_ORIGIN_SLANG
#define VKRT_GEOMETRY_SURFACE_RAY_ORIGIN_SLANG

#include "../../camera/ray.slang"

struct SurfaceRayOrigin {
    float3 position;
    float3 normal;
    float offset;
};

SurfaceRayOrigin makeSurfaceRayOrigin(
    float3 vertex,
    float3 edge1,
    float3 edge2,
    float2 barycentrics,
    float3x4 objectToWorld,
    float3x4 worldToObject
) {
    precise float3 objectPosition = vertex + mad(barycentrics.x, edge1, barycentrics.y * edge2);
    precise float3 worldPosition;
    for (uint axis = 0u; axis < 3u; axis++) {
        worldPosition[axis] =
            objectToWorld[axis][3] +
            mad(objectToWorld[axis][0],
                objectPosition.x,
                mad(objectToWorld[axis][1], objectPosition.y, objectToWorld[axis][2] * objectPosition.z));
    }

    float3 objectNormal = safeNormalize(cross(edge1, edge2));
    float3 worldNormal = mul(transpose(float3x3(worldToObject)), objectNormal);
    float inverseNormalLength = rsqrt(dot(worldNormal, worldNormal));
    worldNormal *= inverseNormalLength;

    float3 extent = abs(edge1) + abs(edge2) + abs(abs(edge1) - abs(edge2));
    float maxExtent = max(extent.x, max(extent.y, extent.z));
    float3 objectError = 5.9604644775390625e-8 * abs(vertex) + 1.7881397695873602e-7 * maxExtent;
    float3 translation = float3(objectToWorld[0][3], objectToWorld[1][3], objectToWorld[2][3]);
    float3 worldError = 1.7881397695873602e-7 * mul(abs(float3x3(objectToWorld)), abs(objectPosition)) +
                        1.1920931797249068e-7 * abs(translation);
    objectError += 1.1920931797249068e-7 * mul(abs(worldToObject), float4(abs(worldPosition), 1.0));

    SurfaceRayOrigin origin;
    origin.position = worldPosition;
    origin.normal = worldNormal;
    origin.offset = dot(worldError, abs(worldNormal)) + inverseNormalLength * dot(objectError, abs(objectNormal));
    return origin;
}

float3 offsetSurfaceRayOrigin(float3 position, float3 normal, float offset) {
    precise float3 origin = mad(offset, normal, position);
    for (uint axis = 0u; axis < 3u; axis++) {
        if (normal[axis] == 0.0) continue;
        if (origin[axis] == 0.0) {
            origin[axis] = normal[axis] > 0.0 ? asfloat(1u) : -asfloat(1u);
        } else {
            int step = (origin[axis] > 0.0) == (normal[axis] > 0.0) ? 1 : -1;
            origin[axis] = asfloat(asint(origin[axis]) + step);
        }
    }
    return origin;
}

#endif
