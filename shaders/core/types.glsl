#ifndef SHADER_TYPES_GLSL
#define SHADER_TYPES_GLSL

#define VKRT_GLSL 1
#include "types.h"
#undef VKRT_GLSL

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

struct Ray {
    vec3 origin;
    vec3 dir;
};

#endif
