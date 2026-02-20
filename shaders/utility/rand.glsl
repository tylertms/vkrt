#ifndef RAND_GLSL
#define RAND_GLSL

const float TWO_PI = 6.28318530718;

// https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
uint pcgHash(uint value) {
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uvec3 pcg3d(uvec3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

uint initRngBase(uvec2 pixel, uint frame) {
    uvec3 mixed = pcg3d(uvec3(pixel, frame));
    return pcgHash(mixed.x ^ mixed.y ^ mixed.z);
}

uint initRngState(uint baseSeed, uint sampleIndex) {
    uint mixed = baseSeed ^ (sampleIndex * 0x9E3779B9u + 0x7F4A7C15u);
    return pcgHash(mixed);
}

uint randUint(inout uint state) {
    state += 0x9E3779B9u;
    uint z = state;
    z = (z ^ (z >> 16u)) * 0x21F0AAADu;
    z = (z ^ (z >> 15u)) * 0x735A2D97u;
    return z ^ (z >> 15u);
}

float rand(inout uint state) {
    return (float(randUint(state)) + 0.5) * (1.0 / 4294967296.0);
}

vec3 randDir(inout uint state) {
    float z = rand(state) * 2.0 - 1.0;
    float a = rand(state) * TWO_PI;
    float r = sqrt(max(0.0, 1.0 - z * z));
    return vec3(r * cos(a), r * sin(a), z);
}

vec3 randHemisphereDir(vec3 normal, inout uint state) {
    vec3 dir = randDir(state);
    return dir * sign(dot(normal, dir));
}

vec2 randUnitCircle(inout uint state) {
    float angle = rand(state) * TWO_PI;
    vec2 point = vec2(cos(angle), sin(angle));
    return point * sqrt(rand(state));
}

#endif
