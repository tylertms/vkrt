#ifndef VKRT_GEOMETRY_SURFACE_TRANSFORM_SLANG
#define VKRT_GEOMETRY_SURFACE_TRANSFORM_SLANG

#include "../../camera/ray.slang"

float3 meshTransformVector(MeshInfo mesh, float3 vector) {
    return mul(float3x3(mesh.objectToWorld), vector);
}

float3 meshTransformNormal(MeshInfo mesh, float3 normal) {
    return safeNormalize(mul(transpose(float3x3(mesh.worldToObject)), normal));
}

float surfaceTransformSign(MeshInfo mesh) {
    return determinant(float3x3(mesh.objectToWorld)) < 0.0 ? -1.0 : 1.0;
}

#endif
