#version 460
#extension GL_EXT_ray_tracing : require
#include "core/types.glsl"

layout(location = 0) rayPayloadInEXT Payload payload;

void main() {
    payload.didHit = false;
    payload.instanceIndex = 0u;
    payload.primitiveIndex = 0u;
    payload.hitDistance = 0.0;
}
