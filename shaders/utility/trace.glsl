#ifndef TRACE_GLSL
#define TRACE_GLSL

#include "fog.glsl"
#include "timeline.glsl"
#include "bsdf/eval.glsl"

vec3 traceSample(ivec2 pixel, inout uint state) {
    ivec2 viewportOrigin = ivec2(scene.viewportRect.xy);
    ivec2 viewportSize = ivec2(scene.viewportRect.zw);
    const float rayMinDistance = 0.001;
    const float rayMaxDistance = 10000.0;
    const uint rayFlags = gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT;

    vec2 jitter = vec2(rand(state), rand(state));
    vec2 viewportPixel = vec2(pixel - viewportOrigin) + jitter;
    vec2 inUV = viewportPixel / vec2(viewportSize);
    vec2 d = inUV * 2.0 - 1.0;

    Ray ray;
    ray.origin = (scene.viewInverse * vec4(0.0, 0.0, 0.0, 1.0)).xyz;

    vec4 viewDir = scene.projInverse * vec4(d.x, d.y, 1.0, 1.0);
    ray.dir = normalize((scene.viewInverse * vec4(viewDir.xyz, 0.0)).xyz);

    vec3 throughput = vec3(1.0);
    vec3 radiance = vec3(0.0);

    payload.time = 0;
    bool timeWindowEnabled = scene.timeBase >= 0.0 && scene.timeStep >= scene.timeBase;
    float timeMax = max(scene.timeStep, 0.0);

    bool tlActive = timelineIsActive();
    float observationTime = tlActive ? scene.timeStep : 0.0;
    float fogDensity = max(scene.fogDensity, 0.0);

    for (uint i = 0; i < max(scene.maxBounces, 1u); i++) {
        float traceDistanceMax = rayMaxDistance;
        if (timeWindowEnabled) {
            float remainingTime = timeMax - payload.time;
            if (remainingTime <= rayMinDistance) break;
            traceDistanceMax = min(traceDistanceMax, remainingTime);
        }

        payload.didHit = false;
        payload.hitDistance = traceDistanceMax;
        traceRayEXT(topLevelAS, rayFlags, 0xFF, 0, 0, 0, ray.origin, rayMinDistance, ray.dir, traceDistanceMax, 0);

        float segmentDistance = payload.didHit ? max(payload.hitDistance, rayMinDistance) : traceDistanceMax;
        float traveledDistance = segmentDistance;

        if (applyFogSegment(fogDensity, segmentDistance, state, ray, throughput, traveledDistance)) {
            payload.time += traveledDistance;
            continue;
        }

        payload.time += traveledDistance;
        if (!payload.didHit) break;

        Material material = materialBuffer.materials[payload.materialIndex];
        if (tlActive && material.emissionStrength > 0.0) {
            TimelineSample emissionSample = sampleTimeline(observationTime - payload.time);
            applyTimelineEmission(material, emissionSample);
        }

        radiance += throughput * (material.emissionColor * material.emissionStrength);

        evalBSDF(payload, material, ray, throughput, state);
    }

    return radiance;
}

#endif
