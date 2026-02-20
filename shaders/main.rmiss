#version 460
#extension GL_EXT_ray_tracing : require
#include "utility/common.glsl"

layout(location = 0) rayPayloadInEXT Payload payload;

void main() {
    payload.didHit = false;
}
