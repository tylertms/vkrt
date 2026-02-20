#version 460
#extension GL_EXT_ray_tracing : require
#include "utility/common.glsl"

layout(location = 0) rayPayloadInEXT Payload payload;

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
};

layout(set = 0, binding = 2, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
} vertexBuffer;

layout(set = 0, binding = 3, std430) readonly buffer IndexBuffer {
    uint indices[];
} indexBuffer;

layout(set = 0, binding = 5, std430) readonly buffer MeshInfoBuffer {
    MeshInfo infos[];
} meshInfo;

hitAttributeEXT vec2 barycentrics;

void main() {
    uint instance = gl_InstanceCustomIndexEXT;
    uint primitiveID = meshInfo.infos[instance].indexBase + gl_PrimitiveID * 3;
    uint vertexBase = meshInfo.infos[instance].vertexBase;

    uint index0 = indexBuffer.indices[primitiveID + 0] + vertexBase;
    uint index1 = indexBuffer.indices[primitiveID + 1] + vertexBase;
    uint index2 = indexBuffer.indices[primitiveID + 2] + vertexBase;

    vec3 normal0 = vertexBuffer.vertices[index0].normal;
    vec3 normal1 = vertexBuffer.vertices[index1].normal;
    vec3 normal2 = vertexBuffer.vertices[index2].normal;

    vec3 pos0 = vertexBuffer.vertices[index0].pos;
    vec3 pos1 = vertexBuffer.vertices[index1].pos;
    vec3 pos2 = vertexBuffer.vertices[index2].pos;

    float u = barycentrics.x;
    float v = barycentrics.y;

    vec3 localNormal = normalize(normal0 * (1.0 - u - v) + normal1 * u + normal2 * v);
    vec3 localPos = normalize(pos0 * (1.0 - u - v) + pos1 * u + pos2 * v);
    vec3 worldNormal = normalize(localNormal * mat3(gl_ObjectToWorld3x4EXT));
    vec3 worldPos = normalize(localPos * mat3(gl_ObjectToWorld3x4EXT));

    payload.point = worldPos;
    payload.didHit = true;
    payload.normal = worldNormal;
}
