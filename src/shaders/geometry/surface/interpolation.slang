#ifndef VKRT_GEOMETRY_SURFACE_INTERPOLATION_SLANG
#define VKRT_GEOMETRY_SURFACE_INTERPOLATION_SLANG

#include "../../scene/resources.slang"
#include "../packing.slang"
#include "./types.slang"

void loadTriangleVertices(
    MeshInfo mesh,
    uint primitiveIndex,
    out ShaderVertex vertex0,
    out ShaderVertex vertex1,
    out ShaderVertex vertex2
) {
    uint triangleBase = mesh.indexBase + primitiveIndex * 3u;
    uint vertexBase = mesh.vertexBase;

    uint index0 = indices[triangleBase + 0u] + vertexBase;
    uint index1 = indices[triangleBase + 1u] + vertexBase;
    uint index2 = indices[triangleBase + 2u] + vertexBase;

    vertex0 = vertices[index0];
    vertex1 = vertices[index1];
    vertex2 = vertices[index2];
}

float barycentricWeightW(float2 barycentrics) {
    return 1.0 - barycentrics.x - barycentrics.y;
}

float2 interpolateTriangleFloat2(float2 a, float2 b, float2 c, float2 barycentrics) {
    float w = barycentricWeightW(barycentrics);
    return a * w + b * barycentrics.x + c * barycentrics.y;
}

float3 interpolateTriangleFloat3(float3 a, float3 b, float3 c, float2 barycentrics) {
    float w = barycentricWeightW(barycentrics);
    return a * w + b * barycentrics.x + c * barycentrics.y;
}

float4 interpolateTriangleFloat4(float4 a, float4 b, float4 c, float2 barycentrics) {
    float w = barycentricWeightW(barycentrics);
    return a * w + b * barycentrics.x + c * barycentrics.y;
}

SurfaceTextureData evaluateSurfaceTextureData(MeshInfo mesh, uint primitiveIndex, float2 barycentrics) {
    ShaderVertex vertex0, vertex1, vertex2;
    loadTriangleVertices(mesh, primitiveIndex, vertex0, vertex1, vertex2);

    return SurfaceTextureData(
        interpolateTriangleFloat4(
            unpackColorRGBA8(vertex0.packedColor),
            unpackColorRGBA8(vertex1.packedColor),
            unpackColorRGBA8(vertex2.packedColor),
            barycentrics
        ),
        interpolateTriangleFloat2(vertex0.texcoord0, vertex1.texcoord0, vertex2.texcoord0, barycentrics),
        interpolateTriangleFloat2(vertex0.texcoord1, vertex1.texcoord1, vertex2.texcoord1, barycentrics)
    );
}

#endif
