#ifndef VKRT_GEOMETRY_SURFACE_SLANG
#define VKRT_GEOMETRY_SURFACE_SLANG

#include "../scene/resources.slang"
#include "../camera/ray.slang"

// References:
// Triangle normal interpolation: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Shapes/Triangle_Meshes
// Normal transform basis: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Geometry_and_Transformations/Applying_Transformations

struct SurfaceHit {
    uint materialIndex;
    uint frontFace;
    float3 shadingNormal;
    float3 geometricNormal;
    float4 tangent;
    float4 color;
    float2 texcoord0;
    float2 texcoord1;
};

void loadTriangleVertices(MeshInfo mesh, uint primitiveIndex, out Vertex vertex0, out Vertex vertex1, out Vertex vertex2) {
    uint triangleBase = mesh.indexBase + primitiveIndex * 3u;
    uint vertexBase = mesh.vertexBase;

    uint index0 = indices[triangleBase + 0u] + vertexBase;
    uint index1 = indices[triangleBase + 1u] + vertexBase;
    uint index2 = indices[triangleBase + 2u] + vertexBase;

    vertex0 = vertices[index0];
    vertex1 = vertices[index1];
    vertex2 = vertices[index2];
}

float2 evaluateSurfaceTexcoordSet(MeshInfo mesh, uint primitiveIndex, float2 barycentrics, uint texcoordSet) {
    Vertex vertex0, vertex1, vertex2;
    loadTriangleVertices(mesh, primitiveIndex, vertex0, vertex1, vertex2);

    float u = barycentrics.x;
    float v = barycentrics.y;
    float w = 1.0 - u - v;
    if (texcoordSet == 1u) {
        return vertex0.texcoord1 * w + vertex1.texcoord1 * u + vertex2.texcoord1 * v;
    }
    return vertex0.texcoord0 * w + vertex1.texcoord0 * u + vertex2.texcoord0 * v;
}

float2 evaluateSurfaceTexcoord(MeshInfo mesh, uint primitiveIndex, float2 barycentrics) {
    return evaluateSurfaceTexcoordSet(mesh, primitiveIndex, barycentrics, 0u);
}

float4 evaluateSurfaceColor(MeshInfo mesh, uint primitiveIndex, float2 barycentrics) {
    Vertex vertex0, vertex1, vertex2;
    loadTriangleVertices(mesh, primitiveIndex, vertex0, vertex1, vertex2);

    float u = barycentrics.x;
    float v = barycentrics.y;
    float w = 1.0 - u - v;
    return vertex0.color * w + vertex1.color * u + vertex2.color * v;
}

struct SurfaceAlphaAttributes {
    float4 color;
    float2 texcoord0;
    float2 texcoord1;
};

SurfaceAlphaAttributes evaluateSurfaceAlphaAttributes(MeshInfo mesh, uint primitiveIndex, float2 barycentrics) {
    Vertex vertex0, vertex1, vertex2;
    loadTriangleVertices(mesh, primitiveIndex, vertex0, vertex1, vertex2);

    float u = barycentrics.x;
    float v = barycentrics.y;
    float w = 1.0 - u - v;

    SurfaceAlphaAttributes result;
    result.color = vertex0.color * w + vertex1.color * u + vertex2.color * v;
    result.texcoord0 = vertex0.texcoord0 * w + vertex1.texcoord0 * u + vertex2.texcoord0 * v;
    result.texcoord1 = vertex0.texcoord1 * w + vertex1.texcoord1 * u + vertex2.texcoord1 * v;
    return result;
}

SurfaceHit evaluateSurfaceHit(
    MeshInfo mesh, uint primitiveIndex, float2 barycentrics, float3x4 objectToWorld,
    float3x4 worldToObject, float3 worldRayDirection
) {
    Vertex vertex0, vertex1, vertex2;
    loadTriangleVertices(mesh, primitiveIndex, vertex0, vertex1, vertex2);

    float u = barycentrics.x;
    float v = barycentrics.y;
    float w = 1.0 - u - v;

    float3 objectNormal = safeNormalize(
        vertex0.normal.xyz * w +
        vertex1.normal.xyz * u +
        vertex2.normal.xyz * v);

    float4 objectTangent = vertex0.tangent * w + vertex1.tangent * u + vertex2.tangent * v;
    float handedness = objectTangent.w < 0.0 ? -1.0 : 1.0;

    float3x3 objectToWorldBasis = (float3x3)objectToWorld;
    float3x3 worldToObjectBasis = (float3x3)worldToObject;

    float3 shadingNormalUnoriented = safeNormalize(mul(transpose(worldToObjectBasis), objectNormal));
    float facing = dot(shadingNormalUnoriented, worldRayDirection) > 0.0 ? -1.0 : 1.0;

    float3 worldEdge1 = mul(objectToWorldBasis, vertex1.position.xyz - vertex0.position.xyz);
    float3 worldEdge2 = mul(objectToWorldBasis, vertex2.position.xyz - vertex0.position.xyz);
    float3 geometricNormal = safeNormalize(cross(worldEdge1, worldEdge2));
    if (determinant(objectToWorldBasis) < 0.0) {
        geometricNormal = -geometricNormal;
    }

    SurfaceHit hit;
    hit.materialIndex = mesh.materialIndex;
    hit.frontFace = dot(geometricNormal, worldRayDirection) < 0.0 ? 1u : 0u;
    hit.shadingNormal = shadingNormalUnoriented * facing;
    hit.geometricNormal = hit.frontFace != 0u ? geometricNormal : -geometricNormal;
    hit.tangent = float4(safeNormalize(mul(objectToWorldBasis, objectTangent.xyz)) * facing, handedness);
    hit.color = vertex0.color * w + vertex1.color * u + vertex2.color * v;
    hit.texcoord0 = vertex0.texcoord0 * w + vertex1.texcoord0 * u + vertex2.texcoord0 * v;
    hit.texcoord1 = vertex0.texcoord1 * w + vertex1.texcoord1 * u + vertex2.texcoord1 * v;

    return hit;
}

#endif
