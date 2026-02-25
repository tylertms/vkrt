#ifndef BINDINGS_GLSL
#define BINDINGS_GLSL

#include "common.glsl"

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
} scene;

layout(set = 0, binding = 7, std430) readonly buffer MeshInfoBuffer {
    MeshInfo infos[];
} meshInfo;

layout(set = 0, binding = 8, std430) readonly buffer MaterialBuffer {
    Material materials[];
} materialBuffer;

#endif
