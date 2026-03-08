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
    float specular;
    float specularTint;
    float anisotropic;
    float sheen;
    float sheenTint;
    float clearcoat;
    float clearcoatGloss;
};

struct EmissiveMesh {
    uvec4 indices;
    vec4 emission;
    vec4 stats;
    vec4 bounds;
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
_Static_assert(offsetof(Material, baseColor) == 0, "Material.baseColor must stay std430-aligned");
_Static_assert(offsetof(Material, roughness) == 12, "Material.roughness offset changed");
_Static_assert(offsetof(Material, emissionColor) == 16, "Material.emissionColor offset changed");
_Static_assert(offsetof(Material, emissionLuminance) == 28, "Material.emissionLuminance offset changed");
_Static_assert(offsetof(Material, metallic) == 32, "Material.metallic offset changed");
_Static_assert(offsetof(Material, specular) == 36, "Material.specular offset changed");
_Static_assert(offsetof(Material, specularTint) == 40, "Material.specularTint offset changed");
_Static_assert(offsetof(Material, anisotropic) == 44, "Material.anisotropic offset changed");
_Static_assert(offsetof(Material, sheen) == 48, "Material.sheen offset changed");
_Static_assert(offsetof(Material, sheenTint) == 52, "Material.sheenTint offset changed");
_Static_assert(offsetof(Material, clearcoat) == 56, "Material.clearcoat offset changed");
_Static_assert(offsetof(Material, clearcoatGloss) == 60, "Material.clearcoatGloss offset changed");
_Static_assert(sizeof(Material) == 64, "Material must remain 64 bytes for std430");
#undef uint
#endif

#endif
