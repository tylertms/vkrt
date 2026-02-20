#ifndef BSDF_DIFFUSE
#define BSDF_DIFFUSE

#include "../utility/rand.glsl"

vec3 diffuseBSDF(vec3 normal, inout uint state) {
    return normalize(normal + randDir(state));
}

#endif
