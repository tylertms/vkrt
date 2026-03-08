#ifndef VKRT_SHARED_TYPES_H
#define VKRT_SHARED_TYPES_H

#include "constants.h"

#ifndef VKRT_GLSL
#include <stddef.h>
#include <stdint.h>
#include "cglm.h"

typedef uint32_t uvec4[4];
typedef uint32_t uint;
#endif

struct Vertex {
    vec4 position;
    vec4 normal;
};

struct MeshInfo {
    vec3 position;
    uint vertexBase;
    vec3 rotation;
    uint vertexCount;
    vec3 scale;
    uint indexBase;
    uint indexCount;
    uint materialIndex;
    uint renderBackfaces;
    uint padding;
};

struct Material {
    vec3 baseColor;
    float roughness;
    vec3 emissionColor;
    float emissionLuminance;
    float metallic;
    float subsurface;
    float specular;
    float specularTint;
    float anisotropic;
    float sheen;
    float sheenTint;
    float clearcoat;
    float clearcoatGloss;
    float padding0[3];
};

struct EmissiveMesh {
    uvec4 indices;
    vec4 emission;
    vec4 stats;
};

struct EmissiveTriangle {
    vec4 v0Area;
    vec4 e1Pad;
    vec4 e2Pad;
};

struct SceneData {
    mat4 viewInverse;
    mat4 projInverse;
    uint frameNumber;
    uint samplesPerPixel;
    uint rrMaxDepth;
    uint rrMinDepth;
    uvec4 viewportRect;
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
    vec4 timelineTimeScale[VKRT_SCENE_TIMELINE_MAX_KEYFRAMES];
    vec4 timelineTint[VKRT_SCENE_TIMELINE_MAX_KEYFRAMES];
};

struct PickBuffer {
    uint pixel;
    uint hitMeshIndex;
};

#ifndef VKRT_GLSL
typedef struct Vertex Vertex;
typedef struct MeshInfo MeshInfo;
typedef struct Material Material;
typedef struct EmissiveMesh EmissiveMesh;
typedef struct EmissiveTriangle EmissiveTriangle;
typedef struct SceneData SceneData;
typedef struct PickBuffer PickBuffer;
#undef uint
#endif

#endif
