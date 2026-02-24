#version 460
#extension GL_EXT_ray_tracing : require
#include "utility/bindings.glsl"

layout(location = 0) rayPayloadInEXT Payload payload;

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

    float u = barycentrics.x;
    float v = barycentrics.y;

    vec3 localNormal = normal0 * (1.0 - u - v) + normal1 * u + normal2 * v;
    vec3 worldNormal = normalize(localNormal * mat3(gl_WorldToObjectEXT));
    vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    uint materialIndex = meshInfo.infos[instance].materialIndex;

    payload.point = worldPos;
    payload.didHit = true;
    payload.normal = worldNormal;
    payload.materialIndex = materialIndex;
    payload.time += gl_HitTEXT;
}
