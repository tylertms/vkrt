#ifndef VKRT_LIGHT_DIRECT_LIGHT_SAMPLING_SLANG
#define VKRT_LIGHT_DIRECT_LIGHT_SAMPLING_SLANG

#include "../../sampling/discrete.slang"
#include "../../sampling/random.slang"
#include "../../scene/resources.slang"
#include "./light_types.slang"

LightSample sampleDirectLight(inout uint rng) {
    LightSample lightSample = LightSample();

    uint meshCount = scene.emissiveMeshCount;
    if (meshCount == 0u) return lightSample;

    uint meshIdx = sampleAlias(rand(rng), meshCount, 0u, meshAliasQ, meshAliasIdx);
    EmissiveMesh emissiveMesh = emissiveMeshes[meshIdx];
    if (emissiveMesh.triCount == 0u) return lightSample;

    uint localTri = sampleAlias(rand(rng), emissiveMesh.triCount, emissiveMesh.triOffset, triAliasQ, triAliasIdx);
    EmissiveTriangle triangle = emissiveTriangles[emissiveMesh.triOffset + localTri];

    float u1 = rand(rng);
    float u2 = rand(rng);
    float sqrtU1 = sqrt(u1);
    float b1 = u2 * sqrtU1;
    float b2 = (1.0 - u2) * sqrtU1;

    lightSample.position = triangle.v0Area.xyz + b1 * triangle.e1Pad.xyz + b2 * triangle.e2Pad.xyz;
    lightSample.normal = safeNormalize(cross(triangle.e1Pad.xyz, triangle.e2Pad.xyz));
    lightSample.emission = emissiveMesh.emission;
    lightSample.pdf = emissiveMesh.pmfMesh * emissiveMesh.invTotalArea;
    if (lightSample.pdf > 0.0) {
        lightSample.flags |= VKRT_LIGHT_FLAG_VALID;
    }

    return lightSample;
}

DirectLightSurfaceSample sampleDirectLightSurface(float3 hitPoint, float3 geometricNormal, inout uint rng) {
    DirectLightSurfaceSample sample = {};
    sample.light = sampleDirectLight(rng);
    if (!sample.light.valid()) return sample;

    float3 toLight = sample.light.position - hitPoint;
    float distanceSquared = dot(toLight, toLight);
    if (distanceSquared <= 0.0) return sample;

    float invDistance = rsqrt(distanceSquared);
    float distance = distanceSquared * invDistance;
    sample.wi = toLight * invDistance;

    float cosLight = abs(dot(sample.wi, sample.light.normal));
    if (cosLight <= 0.0) return sample;

    sample.pdfSolidAngle = sample.light.pdf * distanceSquared / cosLight;
    if (sample.pdfSolidAngle <= 0.0) return sample;

    sample.shadowDistance = distance - VKRT_SHADOW_DISTANCE_OFFSET;
    if (sample.shadowDistance <= 0.0) return sample;

    sample.valid = 1u;
    return sample;
}

#endif
