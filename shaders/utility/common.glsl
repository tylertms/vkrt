#ifndef CORE_COMMON_GLSL
#define CORE_COMMON_GLSL

#define MAX_BOUNCES 8

struct Ray {
    vec3 origin;
    vec3 dir;
};

struct Material {
    vec3 baseColor;
    float roughness;
    vec3 emissionColor;
    float emissionStrength;
};

struct Payload {
    vec3 point;
    bool didHit;
    vec3 normal;
    Material material;
};

vec3 toneMapACES(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

#endif
