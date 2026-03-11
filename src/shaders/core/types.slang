#ifndef VKRT_CORE_TYPES_SLANG
#define VKRT_CORE_TYPES_SLANG

#include "../../shared/constants.h"

struct Vertex {
    float4 position;
    float4 normal;
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
    uint padding;
};

struct Material {
    float3 baseColor;
    float roughness;
    float3 emissionColor;
    float emissionLuminance;
    float metallic;
    float specular;
    float specularTint;
    float anisotropic;
    float sheen;
    float sheenTint;
    float clearcoat;
    float clearcoatGloss;
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
    float timeBase;
    float timeStep;
    float fogDensity;
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

struct PickBuffer {
    uint pixel;
    uint hitMeshIndex;
};

struct ShadowPayload {
    uint blocked;
};

#endif
