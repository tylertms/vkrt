#ifndef VKRT_GEOMETRY_SURFACE_RECONSTRUCT_SLANG
#define VKRT_GEOMETRY_SURFACE_RECONSTRUCT_SLANG

#include "../packing.slang"
#include "./interpolation.slang"
#include "./transform.slang"

SurfaceShadingData reconstructSurfaceShading(
    MeshInfo mesh,
    uint primitiveIndex,
    float2 barycentrics,
    float3 worldRayDirection
) {
    ShaderVertex vertex0, vertex1, vertex2;
    loadTriangleVertices(mesh, primitiveIndex, vertex0, vertex1, vertex2);

    float3 objectNormal = safeNormalize(interpolateTriangleFloat3(
        unpackOctNormal(vertex0.packedNormal),
        unpackOctNormal(vertex1.packedNormal),
        unpackOctNormal(vertex2.packedNormal),
        barycentrics
    ));
    float4 objectTangent = interpolateTriangleFloat4(
        unpackOctTangent(vertex0.packedTangent),
        unpackOctTangent(vertex1.packedTangent),
        unpackOctTangent(vertex2.packedTangent),
        barycentrics
    );
    float handedness = objectTangent.w < 0.0 ? -1.0 : 1.0;

    float3 shadingNormalUnoriented = meshTransformNormal(mesh, objectNormal);
    float facing = dot(shadingNormalUnoriented, worldRayDirection) > 0.0 ? -1.0 : 1.0;

    float3 worldEdge1 = meshTransformVector(mesh, vertex1.position.xyz - vertex0.position.xyz);
    float3 worldEdge2 = meshTransformVector(mesh, vertex2.position.xyz - vertex0.position.xyz);
    float3 geometricNormal = safeNormalize(cross(worldEdge1, worldEdge2));
    if (surfaceTransformSign(mesh) < 0.0) {
        geometricNormal = -geometricNormal;
    }

    SurfaceShadingData hit;
    hit.materialIndex = mesh.materialIndex;
    hit.frontFace = dot(geometricNormal, worldRayDirection) < 0.0 ? 1u : 0u;
    hit.shadingNormal = shadingNormalUnoriented * facing;
    hit.geometricNormal = hit.frontFace != 0u ? geometricNormal : -geometricNormal;
    hit.tangent = float4(safeNormalize(meshTransformVector(mesh, objectTangent.xyz)) * facing, handedness);
    hit.textureData = evaluateSurfaceTextureData(mesh, primitiveIndex, barycentrics);
    return hit;
}

#endif
