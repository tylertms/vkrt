#ifndef VKRT_SHARED_TYPES_H
#define VKRT_SHARED_TYPES_H

#include "constants.h"

#ifndef VKRT_SHADER
#include "cglm.h"

#include <stdint.h>

typedef vec4 float4;
typedef vec3 float3;
typedef vec2 float2;
typedef uint32_t uint;
typedef uint uint4[4];
typedef mat4 float4x4;
#endif

#ifdef VKRT_SHADER
#define VKRT_SHARED_STRUCT(name, fields) struct name fields;
#else
#define VKRT_SHARED_STRUCT(name, fields) typedef struct fields name;
#endif

VKRT_SHARED_STRUCT(Vertex, {
    float4 position;
    float4 normal;
    float4 tangent;
    float4 color;
    float2 texcoord0;
    float2 texcoord1;
})

VKRT_SHARED_STRUCT(MeshInfo, {
    float3 position;
    uint vertexBase;
    float3 rotation;
    uint vertexCount;
    float3 scale;
    uint indexBase;
    uint indexCount;
    uint materialIndex;
    uint renderBackfaces;
    float lightPdfArea;
    float opacity;
    uint reserved0;
    uint reserved1;
    uint reserved2;
})

VKRT_SHARED_STRUCT(Material, {
    float3 baseColor;
    float roughness;
    float3 emissionColor;
    float emissionLuminance;
    float3 eta;
    float metallic;
    float3 k;
    float anisotropic;
    float specular;
    float specularTint;
    float abbeNumber;
    float reserved0;
    float4 sheenTintWeight;
    float clearcoat;
    float clearcoatGloss;
    float ior;
    float diffuseRoughness;
    float transmission;
    float subsurface;
    float sheenRoughness;
    float absorptionCoefficient;
    float3 attenuationColor;
    float normalTextureScale;
    uint baseColorTextureIndex;
    uint metallicRoughnessTextureIndex;
    uint normalTextureIndex;
    uint emissiveTextureIndex;
    uint baseColorTextureWrap;
    uint metallicRoughnessTextureWrap;
    uint normalTextureWrap;
    uint emissiveTextureWrap;
    float opacity;
    float alphaCutoff;
    uint alphaMode;
    uint textureTexcoordSets;
    float4 baseColorTextureTransform;
    float4 metallicRoughnessTextureTransform;
    float4 normalTextureTransform;
    float4 emissiveTextureTransform;
    float4 textureRotations;
})

VKRT_SHARED_STRUCT(EmissiveMesh, {
    uint triOffset;
    uint triCount;
    float pmfMesh;
    float invTotalArea;
    float3 emission;
    float reserved0;
})

VKRT_SHARED_STRUCT(EmissiveTriangle, {
    float4 v0Area;
    float4 e1Pad;
    float4 e2Pad;
})

VKRT_SHARED_STRUCT(RGB2SpecTableInfo, {
    uint res;
    uint scaleOffset;
    uint dataOffset;
})

#if defined(_MSC_VER) && !defined(VKRT_SHADER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
VKRT_SHARED_STRUCT(SceneData, {
    float4x4 viewInverse;
    float4x4 projInverse;
    uint frameNumber;
    uint samplesPerPixel;
    uint rrMaxDepth;
    uint rrMinDepth;
    uint4 viewportRect;
    uint packedRenderSettings;
    float exposure;
    float timeBase;
    float timeStep;
    float4 environmentLight;
    uint environmentTextureIndex;
    float environmentRotation;
    uint debugMode;
    uint misNeeEnabled;
    uint emissiveMeshCount;
    uint emissiveTriangleCount;
    uint selectionEnabled;
    uint selectedMeshIndex;
    RGB2SpecTableInfo rgb2specSRGB;
})

#if defined(_MSC_VER) && !defined(VKRT_SHADER)
#pragma warning(pop)
#endif

VKRT_SHARED_STRUCT(Selection, {
    uint pixel;
    uint hitMeshIndex;
})

#undef VKRT_SHARED_STRUCT

#endif
