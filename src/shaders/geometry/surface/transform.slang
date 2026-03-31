#ifndef VKRT_GEOMETRY_SURFACE_TRANSFORM_SLANG
#define VKRT_GEOMETRY_SURFACE_TRANSFORM_SLANG

#include "../../camera/ray.slang"

static const float VKRT_SURFACE_DEG_TO_RAD = 0.01745329251994329577;
static const float VKRT_SURFACE_SCALE_EPSILON = 1e-6;

float3 rotateX(float3 value, float angleRadians) {
    float s = sin(angleRadians);
    float c = cos(angleRadians);
    return float3(value.x, c * value.y - s * value.z, s * value.y + c * value.z);
}

float3 rotateY(float3 value, float angleRadians) {
    float s = sin(angleRadians);
    float c = cos(angleRadians);
    return float3(c * value.x + s * value.z, value.y, -s * value.x + c * value.z);
}

float3 rotateZ(float3 value, float angleRadians) {
    float s = sin(angleRadians);
    float c = cos(angleRadians);
    return float3(c * value.x - s * value.y, s * value.x + c * value.y, value.z);
}

float3 rotateMeshVector(float3 value, float3 rotationDegrees) {
    float3 rotationRadians = rotationDegrees * VKRT_SURFACE_DEG_TO_RAD;
    return rotateZ(rotateY(rotateX(value, rotationRadians.x), rotationRadians.y), rotationRadians.z);
}

float safeSignedReciprocal(float value) {
    if (abs(value) > VKRT_SURFACE_SCALE_EPSILON) {
        return 1.0 / value;
    }

    return value < 0.0 ? -1.0 / VKRT_SURFACE_SCALE_EPSILON : 1.0 / VKRT_SURFACE_SCALE_EPSILON;
}

float3 meshTransformVector(MeshInfo mesh, float3 vector) {
    return rotateMeshVector(vector * mesh.scale, mesh.rotation);
}

float3 meshTransformNormal(MeshInfo mesh, float3 normal) {
    float3 invScale = float3(
        safeSignedReciprocal(mesh.scale.x),
        safeSignedReciprocal(mesh.scale.y),
        safeSignedReciprocal(mesh.scale.z)
    );
    return safeNormalize(rotateMeshVector(normal * invScale, mesh.rotation));
}

float surfaceTransformSign(MeshInfo mesh) {
    return mesh.scale.x * mesh.scale.y * mesh.scale.z < 0.0 ? -1.0 : 1.0;
}

#endif
