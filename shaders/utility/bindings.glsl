#ifndef SHADER_BINDINGS_GLSL
#define SHADER_BINDINGS_GLSL

#include "core/types.glsl"

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
    SceneData scene;
};

layout(set = 0, binding = 7, std430) buffer PickBufferBlock {
    PickBuffer pickBuffer;
};

layout(set = 0, binding = 8, std430) readonly buffer MeshInfoBuffer {
    MeshInfo infos[];
} meshInfo;

layout(set = 0, binding = 9, std430) readonly buffer MaterialBuffer {
    Material materials[];
} materialBuffer;

layout(set = 0, binding = 10, std430) readonly buffer EmissiveMeshBuffer {
    EmissiveMesh meshes[];
} emissiveMeshBuffer;

layout(set = 0, binding = 11, std430) readonly buffer EmissiveTriangleBuffer {
    EmissiveTriangle triangles[];
} emissiveTriangleBuffer;

#endif
