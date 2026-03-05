#ifndef SHADER_TYPES_GLSL
#define SHADER_TYPES_GLSL

struct Ray {
    vec3 origin;
    vec3 dir;
};

struct Material {
    vec3 baseColor;
    float metallic;
    float roughness;
    float specular;
    float specularTint;
    float anisotropic;
    float sheen;
    float sheenTint;
    float clearcoat;
    float clearcoatGloss;
    float subsurface;
    float transmission;
    float ior;
    float padding0[5];
    vec3 emissionColor;
    float emissionLuminance;
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
