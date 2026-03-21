#ifndef VKRT_SHARED_TYPES_H
#define VKRT_SHARED_TYPES_H

#include "constants.h"

#ifndef VKRT_SHADER
#include <stddef.h>
#include <stdint.h>
#include "cglm.h"

typedef vec4 float4;
typedef vec3 float3;
typedef vec2 float2;
typedef uint32_t uint;
typedef uint uint4[4];
typedef mat4 float4x4;
#endif

struct Vertex {
    float4 position;
    float4 normal;
    float4 tangent;
    float4 color;
    float2 texcoord0;
    float2 texcoord1;
};

struct MeshInfo {
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
    uint _meshPad0;
    uint _meshPad1;
    uint _meshPad2;
};

struct Material {
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
    float _materialPad0;
    float _materialPad1;
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
};

struct EmissiveMesh {
    uint triOffset;
    uint triCount;
    float pmfMesh;
    float invTotalArea;
    float3 emission;
    float _pad0;
};

struct EmissiveTriangle {
    float4 v0Area;
    float4 e1Pad;
    float4 e2Pad;
};

struct SceneData {
    float4x4 viewInverse;
    float4x4 projInverse;
    uint frameNumber;
    uint samplesPerPixel;
    uint rrMaxDepth;
    uint rrMinDepth;
    uint4 viewportRect;
    uint toneMappingMode;
    float exposure;
    float timeBase;
    float timeStep;
    float4 environmentLight;
    uint debugMode;
    uint misNeeEnabled;
    uint timelineEnabled;
    uint timelineKeyframeCount;
    uint emissiveMeshCount;
    uint emissiveTriangleCount;
    uint selectionEnabled;
    uint selectedMeshIndex;
    float4 timelineTimeScale[VKRT_SCENE_TIMELINE_MAX_KEYFRAMES];
    float4 timelineTint[VKRT_SCENE_TIMELINE_MAX_KEYFRAMES];
};

struct Selection {
    uint pixel;
    uint hitMeshIndex;
};

#ifndef VKRT_SHADER
typedef struct Vertex Vertex;
typedef struct MeshInfo MeshInfo;
typedef struct Material Material;
typedef struct EmissiveMesh EmissiveMesh;
typedef struct EmissiveTriangle EmissiveTriangle;
typedef struct SceneData SceneData;
typedef struct Selection Selection;
#endif

#endif
