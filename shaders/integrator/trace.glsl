#ifndef SHADER_TRACE_GLSL
#define SHADER_TRACE_GLSL

#include "utility/fog.glsl"
#include "utility/timeline.glsl"
#include "core/mis.glsl"
#include "bsdf/bsdf.glsl"
#include "lighting/direct.glsl"

bool applyRussianRoulette(inout vec3 throughput, inout uint state, uint bounce) {
    if (bounce < 3u) return true;

    float continueProbability = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
    if (rand(state) > continueProbability) return false;
    throughput /= continueProbability;
    return true;
}

bool traceShadowVisibility(vec3 origin, vec3 direction, float maxDistance, float rayMinDistance) {
    if (!(maxDistance > rayMinDistance)) return false;

    const uint shadowRayFlags = gl_RayFlagsOpaqueEXT
        | gl_RayFlagsTerminateOnFirstHitEXT
        | gl_RayFlagsSkipClosestHitShaderEXT;

    payload.didHit = true;
    traceRayEXT(topLevelAS, shadowRayFlags, 0xFF, 0, 0, 0, origin, rayMinDistance, direction, maxDistance, 0);
    return !payload.didHit;
}

vec3 debugBounceHeatmap(uint bounces, uint maxBounces) {
    float t = clamp(float(bounces) / float(maxBounces), 0.0, 1.0);
    if (t < 0.5) return mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), t * 2.0);
    return mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t - 0.5) * 2.0);
}

float debugDepthValue(float distance) {
    float scaledDistance = max(distance, 0.0) * 0.25;
    return 1.0 / (1.0 + scaledDistance);
}

vec3 trace(ivec2 pixel, inout uint state) {
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

    uint debugMode = scene.debugMode;

    if (debugMode == 1u || debugMode == 2u) {
        payload.didHit = false;
        payload.hitDistance = rayMaxDistance;
        traceRayEXT(topLevelAS, rayFlags, 0xFF, 0, 0, 0, ray.origin, rayMinDistance, ray.dir, rayMaxDistance, 0);
        if (!payload.didHit) return vec3(0.0);
        if (debugMode == 1u) return payload.normal * 0.5 + 0.5;
        return vec3(debugDepthValue(payload.hitDistance));
    }

    bool neeOn = scene.neeEnabled != 0u;
    bool misOn = scene.misEnabled != 0u && neeOn;
    bool neeOnly = debugMode == 4u;
    bool bsdfOnly = debugMode == 5u;

    vec3 throughput = vec3(1.0);
    vec3 radiance = vec3(0.0);
    uint bounceCount = 0u;

    payload.time = 0;
    bool timeWindowEnabled = scene.timeBase >= 0.0 && scene.timeStep >= scene.timeBase;
    float timeMax = max(scene.timeStep, 0.0);

    bool tlActive = timelineIsActive();
    float observationTime = tlActive ? scene.timeStep : 0.0;
    float fogDensity = max(scene.fogDensity, 0.0);
    bool hasPrevSample = false;
    float prevSamplePdf = 0.0;

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

        if (fogDensity > 0.0 && segmentDistance > rayMinDistance) {
            float scatterDistance = sampleExponentialDistance(fogDensity, state);

            if (scatterDistance < segmentDistance) {
                traveledDistance = max(scatterDistance, rayMinDistance);
                throughput *= exp(-fogDensity * traveledDistance);
                payload.time += traveledDistance;
                ray.origin += ray.dir * traveledDistance;
                bounceCount++;

                if (neeOn && !bsdfOnly) {
                    DirectLightSample directSample;
                    if (sampleDirectLight(ray.origin, payload.time, observationTime, tlActive, state, directSample)
                        && directSample.pdf > 1e-8)
                    {
                        float shadowDistance = directSample.distance - rayMinDistance;
                        if (shadowDistance > rayMinDistance
                            && traceShadowVisibility(ray.origin,
                                                     directSample.wi, shadowDistance, rayMinDistance))
                        {
                            float phase = mediumIsotropicPhase();
                            float misWeight = misOn ? misPowerWeight(directSample.pdf, phase) : 1.0;
                            radiance += throughput * directSample.radiance *
                                (phase * exp(-fogDensity * directSample.distance) * misWeight / directSample.pdf);
                        }
                    }
                }

                ray.dir = randDir(state);
                hasPrevSample = true;
                prevSamplePdf = mediumIsotropicPhase();

                if (!applyRussianRoulette(throughput, state, i)) break;
                continue;
            }

            throughput *= exp(-fogDensity * segmentDistance);
        }

        payload.time += traveledDistance;
        if (!payload.didHit) break;
        bounceCount++;

        Material material = materialBuffer.materials[payload.materialIndex];
        if (tlActive && material.emissionStrength > 0.0) {
            TimelineSample emissionSample = sampleTimeline(observationTime - payload.time);
            applyTimelineEmission(material, emissionSample);
        }
        bool emissiveSurface = material.emissionStrength > 0.0;

        if (!neeOnly && material.emissionStrength > 0.0) {
            vec3 emitted = material.emissionColor * material.emissionStrength;
            float emissionWeight = 1.0;
            if (misOn && hasPrevSample) {
                float lightPdf = lightPdfForEmitterHit(payload.instanceIndex, payload.primitiveIndex, ray.origin, payload.point);
                emissionWeight = misPowerWeight(prevSamplePdf, lightPdf);
            }
            radiance += throughput * emitted * emissionWeight;
        }

        if (neeOn && !bsdfOnly && !emissiveSurface) {
            DirectLightSample directSample;
            if (sampleDirectLight(payload.point, payload.time, observationTime, tlActive, state, directSample)
                && directSample.pdf > 1e-8)
            {
                float cosSurface = max(dot(payload.normal, directSample.wi), 0.0);
                float shadowDistance = directSample.distance - rayMinDistance;
                if (cosSurface > 0.0 && shadowDistance > rayMinDistance
                    && traceShadowVisibility(payload.point,
                                             directSample.wi, shadowDistance, rayMinDistance))
                {
                    vec3 f = evalBSDF(payload.normal, directSample.wi, -ray.dir, material);
                    float bsdfPdf = pdfBSDF(payload.normal, directSample.wi, -ray.dir, material);
                    float misWeight = misOn ? misPowerWeight(directSample.pdf, bsdfPdf) : 1.0;
                    radiance += throughput * f * directSample.radiance *
                        (cosSurface * exp(-fogDensity * directSample.distance) * misWeight / directSample.pdf);
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

        if (!applyRussianRoulette(throughput, state, i)) break;

        ray.dir = bsdfSample.incoming;
        ray.origin = payload.point;
    }

    if (debugMode == 3u) return debugBounceHeatmap(bounceCount, max(scene.maxBounces, 1u));

    return radiance;
}

#endif
