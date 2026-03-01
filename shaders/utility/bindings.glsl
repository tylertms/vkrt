#ifndef SHADER_BINDINGS_GLSL
#define SHADER_BINDINGS_GLSL

#include "constants.h"
#include "core/types.glsl"

struct Vertex {
    vec3 pos;
    vec3 normal;
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
    uint padding0;
};

struct EmissiveMesh {
    uvec4 indices;   // x: meshIndex, y: triangleOffset, z: triangleCount
    vec4 emission;   // rgb: emissionColor, a: emissionStrength
    vec4 stats;      // x: cumulativeMeshCdf, y: totalArea, z: selectionProbability
};

struct EmissiveTriangle {
    vec4 v0Area;     // xyz: world-space vertex 0, w: area
    vec4 e1Pad;      // xyz: edge from v0 to v1, w: cumulativeAreaInMesh
    vec4 e2Pad;      // xyz: edge from v0 to v2
};

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1, rgba32f) uniform image2D accumulationReadImage;
layout(set = 0, binding = 2, rgba32f) uniform image2D accumulationWriteImage;
layout(set = 0, binding = 3, rgba16) uniform image2D outputImage;

layout(set = 0, binding = 4, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
} vertexBuffer;

layout(set = 0, binding = 5, std430) readonly buffer IndexBuffer {
    uint indices[];
} indexBuffer;

layout(set = 0, binding = 6) uniform SceneUniform {
    mat4 viewInverse;
    mat4 projInverse;
    uint frameNumber;
    uint samplesPerPixel;
    uint maxBounces;
    uint toneMappingMode;
    uvec4 viewportRect;
    float timeBase;
    float timeStep;
    float fogDensity;
    uint debugMode;
    uint timelineEnabled;
    uint timelineKeyframeCount;
    uint emissiveMeshCount;
    uint emissiveTriangleCount;
    uint neeEnabled;
    uint misEnabled;
    uint padding0;
    uint padding1;
    vec4 timelineTimeScale[VKRT_SCENE_TIMELINE_MAX_KEYFRAMES];
    vec4 timelineTint[VKRT_SCENE_TIMELINE_MAX_KEYFRAMES];
} scene;

layout(set = 0, binding = 7, std430) readonly buffer MeshInfoBuffer {
    MeshInfo infos[];
} meshInfo;

layout(set = 0, binding = 8, std430) readonly buffer MaterialBuffer {
    Material materials[];
} materialBuffer;

layout(set = 0, binding = 9, std430) readonly buffer EmissiveMeshBuffer {
    EmissiveMesh meshes[];
} emissiveMeshBuffer;

layout(set = 0, binding = 10, std430) readonly buffer EmissiveTriangleBuffer {
    EmissiveTriangle triangles[];
} emissiveTriangleBuffer;

#endif
