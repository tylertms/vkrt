#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 color;

void main() {
    color = vec3(0.1);
}
