#ifndef RAND_GLSL
#define RAND_GLSL

const float TWO_PI = 6.28318530718;

// https://www.reedbeta.com/blog/hash-functions-for-gpu-rendering/
uint pcgHash(uint value) {
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint initRngBase(uvec2 pixel, uint frame) {
    uint seed = pixel.x * 0x8DA6B343u;
    seed ^= pixel.y * 0xD8163841u;
    seed ^= frame * 0xCB1AB31Fu;
    return pcgHash(seed);
}

uint initRngState(uint baseSeed, uint sampleIndex) {
    return baseSeed + sampleIndex * 0x9E3779B9u;
}

uint randUint(inout uint state) {
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
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

#endif
