#ifndef SHADER_FOG_GLSL
#define SHADER_FOG_GLSL

#include "core/math.glsl"
#include "utility/rand.glsl"

float mediumIsotropicPhase() {
    return 0.25 * INV_PI;
}

float sampleExponentialDistance(float sigmaT, inout uint state) {
    float u = max(1.0 - rand(state), 1e-6);
    return -log(u) / sigmaT;
}

#endif
