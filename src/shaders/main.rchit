#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 color;

struct Vertex {
    vec3 pos;
    vec3 normal;
};

struct MeshInfo {
    uint vertexBase;
    uint triBase;
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
    uint inst = gl_InstanceCustomIndexEXT;
    uint primID = meshInfo.infos[inst].triBase + gl_PrimitiveID;
    uint vbase = meshInfo.infos[inst].vertexBase;
    
    uint index0 = indexBuffer.indices[primID*3 + 0] + vbase;
    uint index1 = indexBuffer.indices[primID*3 + 1] + vbase;
    uint index2 = indexBuffer.indices[primID*3 + 2] + vbase;

    vec3 normal0 = vertexBuffer.vertices[index0].normal;
    vec3 normal1 = vertexBuffer.vertices[index1].normal;
    vec3 normal2 = vertexBuffer.vertices[index2].normal;

    float u = barycentrics.x;
    float v = barycentrics.y;
    vec3 interp = normalize(mix(mix(normal0, normal1, u), normal2, v));

    color = interp * 0.5 + 0.5;
}

