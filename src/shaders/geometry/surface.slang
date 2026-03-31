#ifndef VKRT_GEOMETRY_SURFACE_SLANG
#define VKRT_GEOMETRY_SURFACE_SLANG

#include "../camera/ray.slang"
#include "../scene/resources.slang"
#include "packing.slang"

// References:
// Triangle normal interpolation: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Shapes/Triangle_Meshes
// Normal transform basis: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Geometry_and_Transformations/Applying_Transformations

static const float VKRT_SURFACE_DEG_TO_RAD = 0.01745329251994329577;
static const float VKRT_SURFACE_SCALE_EPSILON = 1e-6;

struct SurfaceTextureData {
    float4 color = float4(1.0);
    float2 texcoord0 = float2(0.0);
    float2 texcoord1 = float2(0.0);

    __init() {}

    __init(float4 color, float2 texcoord0, float2 texcoord1) {
        this.color = color;
        this.texcoord0 = texcoord0;
        this.texcoord1 = texcoord1;
    }
};

struct SurfaceShadingData {
    uint materialIndex = VKRT_INVALID_INDEX;
    uint frontFace = 0u;
    float3 shadingNormal = float3(0.0, 0.0, 1.0);
    float3 geometricNormal = float3(0.0, 0.0, 1.0);
    float4 tangent = float4(1.0, 0.0, 0.0, 1.0);
    SurfaceTextureData textureData = SurfaceTextureData();
};

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
    float u = barycentrics.x;
    float v = barycentrics.y;
    return 1.0 - u - v;
}

float2 interpolateTriangleFloat2(float2 a, float2 b, float2 c, float2 barycentrics) {
    float u = barycentrics.x;
    float v = barycentrics.y;
    float w = barycentricWeightW(barycentrics);
    return a * w + b * u + c * v;
}

float3 interpolateTriangleFloat3(float3 a, float3 b, float3 c, float2 barycentrics) {
    float u = barycentrics.x;
    float v = barycentrics.y;
    float w = barycentricWeightW(barycentrics);
    return a * w + b * u + c * v;
}

float4 interpolateTriangleFloat4(float4 a, float4 b, float4 c, float2 barycentrics) {
    float u = barycentrics.x;
    float v = barycentrics.y;
    float w = barycentricWeightW(barycentrics);
    return a * w + b * u + c * v;
}

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
        interpolateTriangleFloat2(
            unpackHalf2(vertex0.texcoord0),
            unpackHalf2(vertex1.texcoord0),
            unpackHalf2(vertex2.texcoord0),
            barycentrics
        ),
        interpolateTriangleFloat2(
            unpackHalf2(vertex0.texcoord1),
            unpackHalf2(vertex1.texcoord1),
            unpackHalf2(vertex2.texcoord1),
            barycentrics
        )
    );
}

float2 evaluateSurfaceTexcoordSet(MeshInfo mesh, uint primitiveIndex, float2 barycentrics, uint texcoordSet) {
    SurfaceTextureData surface = evaluateSurfaceTextureData(mesh, primitiveIndex, barycentrics);
    return texcoordSet == 1u ? surface.texcoord1 : surface.texcoord0;
}

float2 evaluateSurfaceTexcoord(MeshInfo mesh, uint primitiveIndex, float2 barycentrics) {
    return evaluateSurfaceTexcoordSet(mesh, primitiveIndex, barycentrics, 0u);
}

float4 evaluateSurfaceColor(MeshInfo mesh, uint primitiveIndex, float2 barycentrics) {
    return evaluateSurfaceTextureData(mesh, primitiveIndex, barycentrics).color;
}

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
    hit.textureData = SurfaceTextureData(
        interpolateTriangleFloat4(
            unpackColorRGBA8(vertex0.packedColor),
            unpackColorRGBA8(vertex1.packedColor),
            unpackColorRGBA8(vertex2.packedColor),
            barycentrics
        ),
        interpolateTriangleFloat2(
            unpackHalf2(vertex0.texcoord0),
            unpackHalf2(vertex1.texcoord0),
            unpackHalf2(vertex2.texcoord0),
            barycentrics
        ),
        interpolateTriangleFloat2(
            unpackHalf2(vertex0.texcoord1),
            unpackHalf2(vertex1.texcoord1),
            unpackHalf2(vertex2.texcoord1),
            barycentrics
        )
    );

    return hit;
}

#endif
