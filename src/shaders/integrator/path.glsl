#ifndef SHADER_PATH_GLSL
#define SHADER_PATH_GLSL

#include "utility/bindings.glsl"
#include "utility/rand.glsl"

bool eventTimeInWindow(float eventTime, bool timeWindowEnabled, float timeMin, float timeMax) {
    if (!timeWindowEnabled) return true;
    return eventTime >= timeMin && eventTime <= timeMax;
}

bool applyRussianRoulette(inout vec3 throughput, inout uint state, uint bounce, uint rrMinDepth) {
    if (bounce < rrMinDepth) return true;

    vec3 absThroughput = abs(throughput);
    float continueProbability = max(absThroughput.r, max(absThroughput.g, absThroughput.b));
    if (continueProbability <= 0.0) return false;
    continueProbability = clamp(continueProbability, 0.05, 1.0);
    if (rand(state) > continueProbability) return false;
    throughput /= continueProbability;
    return true;
}

bool traceShadowVisibility(vec3 origin, vec3 direction, float maxDistance, float rayMinDistance) {
    if (maxDistance <= rayMinDistance) return false;

    const uint shadowRayFlags = gl_RayFlagsOpaqueEXT
            | gl_RayFlagsTerminateOnFirstHitEXT
            | gl_RayFlagsSkipClosestHitShaderEXT;

    payload.didHit = true;
    traceRayEXT(topLevelAS, shadowRayFlags, 0xFF, 0, 0, 0, origin, rayMinDistance, direction, maxDistance, 0);
    return !payload.didHit;
}

#endif
