#ifndef SHADER_BSDF_TYPES_GLSL
#define SHADER_BSDF_TYPES_GLSL

struct BSDFSample {
    vec3 incoming;
    vec3 f;
    float pdf;
};

struct BSDFEval {
    vec3 f;
    float pdf;
};

struct SurfaceState {
    vec3 spec0;
    vec3 sheenColor;
    float diffuseWeight;
    float specularWeight;
    float clearcoatWeight;
    float sheenWeight;
    float diffuseRoughness;
    float ax;
    float ay;
    float clearcoatAlpha;
    float sheenRoughness;
};

#endif
