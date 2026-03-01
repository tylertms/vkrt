#ifndef SHADER_TYPES_GLSL
#define SHADER_TYPES_GLSL

struct Ray {
    vec3 origin;
    vec3 dir;
};

struct Material {
    vec3 baseColor;
    float roughness;
    vec3 emissionColor;
    float emissionStrength;
    float metallic;
    float padding0;
    float padding1;
    float padding2;
};

struct Payload {
    vec3 point;
    bool didHit;
    vec3 normal;
    uint materialIndex;
    uint instanceIndex;
    uint primitiveIndex;
    float time;
    float hitDistance;
};

#endif
