#ifndef SHADER_TRACE_GLSL
#define SHADER_TRACE_GLSL

#include "utility/fog.glsl"
#include "utility/timeline.glsl"
#include "core/mis.glsl"
#include "bsdf/bsdf.glsl"
#include "lighting/direct.glsl"
#include "integrator/path.glsl"
#include "integrator/debug.glsl"

vec3 trace(ivec2 pixel, vec2 jitter, inout uint state, out uint primaryInstanceIndex) {
    ivec2 viewportOrigin = ivec2(scene.viewportRect.xy);
    ivec2 viewportSize = ivec2(scene.viewportRect.zw);

    const float rayMinDistance = 0.001;
    const float rayMaxDistance = 10000.0;
    const uint rayFlags = gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT;

    primaryInstanceIndex = VKRT_INVALID_INDEX;

    vec2 viewportPixel = vec2(pixel - viewportOrigin) + jitter;
    vec2 inUV = viewportPixel / vec2(viewportSize);
    vec2 d = inUV * 2.0 - 1.0;

    Ray ray;
    ray.origin = (scene.viewInverse * vec4(0.0, 0.0, 0.0, 1.0)).xyz;

    vec4 viewDir = scene.projInverse * vec4(d.x, d.y, 1.0, 1.0);
    ray.dir = normalize((scene.viewInverse * vec4(viewDir.xyz, 0.0)).xyz);

    uint debugMode = scene.debugMode;
    bool misNeeEnabled = scene.misNeeEnabled != 0u;

    if (debugMode == VKRT_DEBUG_MODE_NORMALS || debugMode == VKRT_DEBUG_MODE_DEPTH || debugMode == VKRT_DEBUG_MODE_FRESNEL) {
        payload.didHit = false;
        payload.hitDistance = rayMaxDistance;

        traceRayEXT(topLevelAS, rayFlags, 0xFF, 0, 0, 0, ray.origin, rayMinDistance, ray.dir, rayMaxDistance, 0);

        primaryInstanceIndex = payload.didHit ? payload.instanceIndex : VKRT_INVALID_INDEX;

        if (!payload.didHit) return vec3(0.0);
        if (debugMode == VKRT_DEBUG_MODE_NORMALS) return payload.normal * 0.5 + 0.5;
        if (debugMode == VKRT_DEBUG_MODE_DEPTH) return vec3(debugDepthValue(payload.hitDistance));

        Material material = materialBuffer.materials[payload.materialIndex];
        vec3 outgoing = -ray.dir;
        float cosNV = max(dot(payload.normal, outgoing), 0.0);
        return fresnelSchlick(cosNV, material);
    }

    bool neeOnly = debugMode == VKRT_DEBUG_MODE_NEE_ONLY;
    bool bsdfOnly = debugMode == VKRT_DEBUG_MODE_BSDF_ONLY;

    vec3 throughput = vec3(1.0);
    vec3 radiance = vec3(0.0);
    uint bounceCount = 0u;
    payload.time = 0;
    bool timeWindowEnabled = scene.timeBase >= 0.0 && scene.timeStep >= scene.timeBase;
    float timeMin = max(scene.timeBase, 0.0);
    float timeMax = max(scene.timeStep, 0.0);

    bool tlActive = timelineIsActive();
    float observationTime = tlActive ? scene.timeStep : 0.0;
    float fogDensity = max(scene.fogDensity, 0.0);
    uint rrMinDepth = min(scene.rrMinDepth, max(scene.rrMaxDepth, 1u));
    bool hasPrevSample = false;
    float prevSamplePdf = 0.0;

    for (uint i = 0; i < max(scene.rrMaxDepth, 1u); i++) {
        float traceDistanceMax = rayMaxDistance;
        if (timeWindowEnabled) {
            float remainingTime = timeMax - payload.time;
            if (remainingTime <= rayMinDistance) break;
            traceDistanceMax = min(traceDistanceMax, remainingTime);
        }

        payload.didHit = false;
        payload.hitDistance = traceDistanceMax;
        traceRayEXT(topLevelAS, rayFlags, 0xFF, 0, 0, 0, ray.origin, rayMinDistance, ray.dir, traceDistanceMax, 0);

        if (i == 0u && payload.didHit) {
            primaryInstanceIndex = payload.instanceIndex;
        }

        float segmentDistance = payload.didHit ? max(payload.hitDistance, rayMinDistance) : traceDistanceMax;
        float traveledDistance = segmentDistance;

        if (fogDensity > 0.0 && segmentDistance > rayMinDistance) {
            float scatterDistance = sampleExponentialDistance(fogDensity, state);

            if (scatterDistance < segmentDistance) {
                traveledDistance = max(scatterDistance, rayMinDistance);
                throughput *= exp(-fogDensity * traveledDistance);
                payload.time += traveledDistance;
                ray.origin += ray.dir * traveledDistance;
                bounceCount++;

                if (misNeeEnabled && !bsdfOnly) {
                    DirectLightSample directSample;
                    if (sampleDirectLight(ray.origin, payload.time, observationTime, tlActive, state, directSample)
                            && directSample.pdf > 1e-8)
                    {
                        float directEventTime = payload.time + directSample.distance;
                        if (eventTimeInWindow(directEventTime, timeWindowEnabled, timeMin, timeMax)) {
                            float shadowDistance = directSample.distance - rayMinDistance;
                            if (shadowDistance > rayMinDistance && traceShadowVisibility(ray.origin, directSample.wi, shadowDistance, rayMinDistance))
                            {
                                float misWeight = misPowerWeight(directSample.pdf, ISOTROPIC_PHASE);
                                radiance += throughput * directSample.radiance *
                                        (ISOTROPIC_PHASE * exp(-fogDensity * directSample.distance) * misWeight / directSample.pdf);
                            }
                        }
                    }
                }

                ray.dir = randDir(state);
                hasPrevSample = true;
                prevSamplePdf = ISOTROPIC_PHASE;

                if (!applyRussianRoulette(throughput, state, i, rrMinDepth)) break;
                continue;
            }

            throughput *= exp(-fogDensity * segmentDistance);
        }

        payload.time += traveledDistance;
        if (!payload.didHit) break;
        bounceCount++;

        Material material = materialBuffer.materials[payload.materialIndex];
        bool hitTimeInWindow = eventTimeInWindow(payload.time, timeWindowEnabled, timeMin, timeMax);
        if (tlActive && material.emissionLuminance > 0.0 && hitTimeInWindow) {
            TimelineSample emissionSample = sampleTimeline(observationTime - payload.time);
            applyTimelineEmission(material, emissionSample);
        }
        if (hitTimeInWindow && !neeOnly && material.emissionLuminance > 0.0) {
            vec3 emitted = material.emissionColor * material.emissionLuminance;
            float emissionWeight = 1.0;
            if (misNeeEnabled && hasPrevSample) {
                float lightPdf = lightPdfForEmitterHit(payload.instanceIndex, payload.primitiveIndex, ray.origin, payload.point);
                emissionWeight = misPowerWeight(prevSamplePdf, lightPdf);
            }
            radiance += throughput * emitted * emissionWeight;
        }

        if (misNeeEnabled && !bsdfOnly) {
            DirectLightSample directSample;
            if (sampleDirectLight(payload.point, payload.time, observationTime, tlActive, state, directSample)
                    && directSample.pdf > 1e-8)
            {
                float directEventTime = payload.time + directSample.distance;
                if (eventTimeInWindow(directEventTime, timeWindowEnabled, timeMin, timeMax)) {
                    float cosSurface = max(dot(payload.normal, directSample.wi), 0.0);
                    float shadowDistance = directSample.distance - rayMinDistance;
                    if (cosSurface > 0.0 && shadowDistance > rayMinDistance
                            && traceShadowVisibility(payload.point,
                                directSample.wi, shadowDistance, rayMinDistance))
                    {
                        BSDFEval bsdfEval = evalBSDFAll(payload.normal, directSample.wi, -ray.dir, material);
                        float misWeight = misPowerWeight(directSample.pdf, bsdfEval.pdf);
                        radiance += throughput * bsdfEval.f * directSample.radiance *
                                (cosSurface * exp(-fogDensity * directSample.distance) * misWeight / directSample.pdf);
                    }
                }
            }
        }

        vec3 outgoing = -ray.dir;
        BSDFSample bsdfSample = sampleBSDF(payload.normal, outgoing, material, state);
        if (bsdfSample.pdf < 1e-7 || dot(payload.normal, bsdfSample.incoming) <= 0.0) break;

        vec3 bsdfFactor = bsdfSample.f * dot(payload.normal, bsdfSample.incoming) / bsdfSample.pdf;
        throughput *= bsdfFactor;
        hasPrevSample = true;
        prevSamplePdf = bsdfSample.pdf;

        if (!applyRussianRoulette(throughput, state, i, rrMinDepth)) break;

        ray.dir = bsdfSample.incoming;
        ray.origin = payload.point;
    }

    if (debugMode == VKRT_DEBUG_MODE_BOUNCE_COUNT) return debugBounceHeatmap(bounceCount, max(scene.rrMaxDepth, 1u));

    return radiance;
}

#endif
