#ifndef VKRT_GEOMETRY_SURFACE_SLANG
#define VKRT_GEOMETRY_SURFACE_SLANG

#include "../core/resources.slang"

float3 computeSurfaceNormal(uint instanceIndex, uint primitiveIndex, float2 barycentrics) {
    MeshInfo mesh = meshInfo[instanceIndex];
    uint triangleBase = mesh.indexBase + primitiveIndex * 3u;
    uint vertexBase = mesh.vertexBase;

    uint index0 = indexBuffer[triangleBase + 0u] + vertexBase;
    uint index1 = indexBuffer[triangleBase + 1u] + vertexBase;
    uint index2 = indexBuffer[triangleBase + 2u] + vertexBase;

    float u = barycentrics.x;
    float v = barycentrics.y;
    float w = 1.0 - u - v;

    float3 normal0 = vertexBuffer[index0].normal.xyz;
    float3 normal1 = vertexBuffer[index1].normal.xyz;
    float3 normal2 = vertexBuffer[index2].normal.xyz;
    float3 objectNormal = normalize(normal0 * w + normal1 * u + normal2 * v);

    float3x3 worldToObject = (float3x3)WorldToObject3x4();
    float3 worldNormal = normalize(mul(transpose(worldToObject), objectNormal));
    return faceforward(worldNormal, WorldRayDirection(), worldNormal);
}

#endif
