#ifndef VKRT_GEOMETRY_SURFACE_SLANG
#define VKRT_GEOMETRY_SURFACE_SLANG

#include "../core/resources.slang"
#include "../utility/ray.slang"

// References:
// Triangle normal interpolation: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Shapes/Triangle_Meshes
// Normal transform basis: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Geometry_and_Transformations/Applying_Transformations
// Facing-normal convention: Microsoft, 2024 - https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-faceforward

float3 computeSurfaceNormalUnoriented(
    MeshInfo mesh,
    uint primitiveIndex,
    float2 barycentrics,
    float3x4 worldToObject)
{
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
    float3 objectNormal = safeNormalize(normal0 * w + normal1 * u + normal2 * v);

    float3x3 worldToObjectBasis = (float3x3)worldToObject;
    return safeNormalize(mul(transpose(worldToObjectBasis), objectNormal));
}

float3 computeSurfaceNormal(
    MeshInfo mesh,
    uint primitiveIndex,
    float2 barycentrics,
    float3x4 worldToObject,
    float3 worldRayDirection)
{
    float3 worldNormal = computeSurfaceNormalUnoriented(mesh, primitiveIndex, barycentrics, worldToObject);
    return faceforward(worldNormal, worldRayDirection, worldNormal);
}

float4 computeSurfaceTangent(
    MeshInfo mesh,
    uint primitiveIndex,
    float2 barycentrics,
    float3x4 objectToWorld,
    float3x4 worldToObject,
    float3 worldRayDirection)
{
    uint triangleBase = mesh.indexBase + primitiveIndex * 3u;
    uint vertexBase = mesh.vertexBase;

    uint index0 = indexBuffer[triangleBase + 0u] + vertexBase;
    uint index1 = indexBuffer[triangleBase + 1u] + vertexBase;
    uint index2 = indexBuffer[triangleBase + 2u] + vertexBase;

    float u = barycentrics.x;
    float v = barycentrics.y;
    float w = 1.0 - u - v;

    float4 tangent0 = vertexBuffer[index0].tangent;
    float4 tangent1 = vertexBuffer[index1].tangent;
    float4 tangent2 = vertexBuffer[index2].tangent;
    float4 objectTangent = tangent0 * w + tangent1 * u + tangent2 * v;
    float handedness = objectTangent.w < 0.0 ? -1.0 : 1.0;

    float3 normal0 = vertexBuffer[index0].normal.xyz;
    float3 normal1 = vertexBuffer[index1].normal.xyz;
    float3 normal2 = vertexBuffer[index2].normal.xyz;
    float3 objectNormal = safeNormalize(normal0 * w + normal1 * u + normal2 * v);

    float3x3 objectToWorldBasis = (float3x3)objectToWorld;
    float3x3 worldToObjectBasis = (float3x3)worldToObject;
    float3 worldNormal = safeNormalize(mul(transpose(worldToObjectBasis), objectNormal));
    float3 worldTangent = safeNormalize(mul(objectToWorldBasis, objectTangent.xyz));
    float facing = dot(worldNormal, worldRayDirection) > 0.0 ? -1.0 : 1.0;
    return float4(worldTangent * facing, handedness);
}

float3 computeGeometricNormalUnoriented(
    MeshInfo mesh,
    uint primitiveIndex,
    float3x4 objectToWorld)
{
    uint triangleBase = mesh.indexBase + primitiveIndex * 3u;
    uint vertexBase = mesh.vertexBase;

    uint index0 = indexBuffer[triangleBase + 0u] + vertexBase;
    uint index1 = indexBuffer[triangleBase + 1u] + vertexBase;
    uint index2 = indexBuffer[triangleBase + 2u] + vertexBase;

    float3 p0 = vertexBuffer[index0].position.xyz;
    float3 p1 = vertexBuffer[index1].position.xyz;
    float3 p2 = vertexBuffer[index2].position.xyz;

    float3x3 objectToWorldBasis = (float3x3)objectToWorld;
    float3 worldEdge1 = mul(objectToWorldBasis, p1 - p0);
    float3 worldEdge2 = mul(objectToWorldBasis, p2 - p0);
    float3 normal = safeNormalize(cross(worldEdge1, worldEdge2));
    if (determinant(objectToWorldBasis) < 0.0) normal = -normal;
    return normal;
}

float3 computeSurfaceNormal(uint instanceIndex, uint primitiveIndex, float2 barycentrics) {
    MeshInfo mesh = meshInfo[instanceIndex];
    return computeSurfaceNormal(
        mesh,
        primitiveIndex,
        barycentrics,
        WorldToObject3x4(),
        WorldRayDirection());
}

float4 computeSurfaceTangent(uint instanceIndex, uint primitiveIndex, float2 barycentrics) {
    MeshInfo mesh = meshInfo[instanceIndex];
    return computeSurfaceTangent(
        mesh,
        primitiveIndex,
        barycentrics,
        ObjectToWorld3x4(),
        WorldToObject3x4(),
        WorldRayDirection());
}

float3 computeGeometricNormalUnoriented(uint instanceIndex, uint primitiveIndex) {
    MeshInfo mesh = meshInfo[instanceIndex];
    return computeGeometricNormalUnoriented(mesh, primitiveIndex, ObjectToWorld3x4());
}

float3 computeHitPoint(float3 worldRayOrigin, float3 worldRayDirection, float rayTCurrent) {
    return worldRayOrigin + worldRayDirection * rayTCurrent;
}

#endif
