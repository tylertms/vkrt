#ifndef FOG_GLSL
#define FOG_GLSL

#include "common.glsl"
#include "rand.glsl"

bool applyFogSegment(
    float fogDensity,
    float segmentDistance,
    inout uint state,
    inout Ray ray,
    inout vec3 throughput,
    out float traveledDistance
) {
    const float minFogSample = 1e-6;

    traveledDistance = segmentDistance;
    if (!(fogDensity > 0.0) || !(segmentDistance > 0.0)) {
        return false;
    }

    float fogSample = max(1.0 - rand(state), minFogSample);
    float scatterDistance = -log(fogSample) / fogDensity;
    if (scatterDistance < segmentDistance) {
        throughput *= exp(-fogDensity * scatterDistance);
        traveledDistance = scatterDistance;
        ray.origin += ray.dir * scatterDistance;
        ray.dir = randDir(state);
        return true;
    }

    throughput *= exp(-fogDensity * segmentDistance);
    return false;
}

#endif
