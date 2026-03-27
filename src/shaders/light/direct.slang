#ifndef VKRT_LIGHTING_DIRECT_SLANG
#define VKRT_LIGHTING_DIRECT_SLANG

// References:
// MIS power heuristic: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency
// Direct lighting / NEE: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer
// Alias method: Schwarz, 2011 - https://www.keithschwarz.com/darts-dice-coins/

#include "../bsdf/bsdf.slang"
#include "../sample/sampling.slang"
#include "../scene/resources.slang"
#include "../trace/shadow.slang"

struct LightSample {
    float3 position;
    float3 normal;
    float3 emission;
    float pdf;
    bool valid;
};

struct DirectLightResult {
    float3 radiance;
    bool neeSupported;
};

LightSample sampleLight(inout uint rng) {
    LightSample ls;
    ls.valid = false;

    uint meshCount = scene.emissiveMeshCount;
    if (meshCount == 0u) return ls;

    uint meshIdx = sampleAlias(rand(rng), meshCount, 0u, meshAliasQ, meshAliasIdx);
    EmissiveMesh emesh = emissiveMeshes[meshIdx];

    uint triCount = emesh.triCount;
    if (triCount == 0u) return ls;

    uint localTri = sampleAlias(rand(rng), triCount, emesh.triOffset, triAliasQ, triAliasIdx);
    uint globalTri = emesh.triOffset + localTri;

    EmissiveTriangle tri = emissiveTriangles[globalTri];
    float3 v0 = tri.v0Area.xyz;
    float3 e1 = tri.e1Pad.xyz;
    float3 e2 = tri.e2Pad.xyz;

    float u1 = rand(rng);
    float u2 = rand(rng);
    float su1 = sqrt(u1);
    float b1 = u2 * su1;
    float b2 = (1.0 - u2) * su1;

    ls.position = v0 + b1 * e1 + b2 * e2;
    ls.normal = safeNormalize(cross(e1, e2));
    ls.emission = emesh.emission;
    ls.pdf = emesh.pmfMesh * emesh.invTotalArea;
    ls.valid = ls.pdf > 0.0;

    return ls;
}

float computeBSDFEmitterMISWeight(
    float bsdfPdf,
    float lightPdfArea,
    float3 geometricNormal,
    float3 rayDirection,
    float hitDistance
) {
    if (lightPdfArea <= 0.0) return 1.0;
    float distSq = hitDistance * hitDistance;
    if (distSq <= 0.0) return 1.0;

    float cosLight = abs(dot(rayDirection, geometricNormal));
    if (cosLight <= 0.0) return 0.0;

    float lightPdfSolidAngle = lightPdfArea * distSq / cosLight;
    return powerHeuristic(bsdfPdf, lightPdfSolidAngle);
}

DirectLightResult sampleDirectLight(
    float3 hitPoint,
    float3 geometricNormal,
    ShadingBasis basis,
    BSDFState state,
    MediumState medium,
    inout uint rng
) {
    DirectLightResult result;
    result.radiance = float3(0.0);
    result.neeSupported = true;

    if (absCosTheta(state.wo) <= 0.0) return result;

    LightSample lightSample = sampleLight(rng);
    if (!lightSample.valid) return result;

    float3 toLight = lightSample.position - hitPoint;
    float distSq = dot(toLight, toLight);
    if (distSq <= 0.0) return result;

    float invDist = rsqrt(distSq);
    float dist = distSq * invDist;
    float3 wi = toLight * invDist;

    float cosLight = abs(dot(wi, lightSample.normal));
    if (cosLight <= 0.0) return result;

    float pdfSolidAngle = lightSample.pdf * distSq / cosLight;
    if (pdfSolidAngle <= 0.0) return result;

    float3 shadowOffset = dot(wi, geometricNormal) >= 0.0 ? geometricNormal : -geometricNormal;
    float3 shadowOrigin = hitPoint + shadowOffset * VKRT_SHADOW_ORIGIN_OFFSET;
    float shadowDist = dist - VKRT_SHADOW_DISTANCE_OFFSET;
    if (shadowDist <= 0.0) return result;

    ShadowRayResult shadow = traceShadowRay(shadowOrigin, wi, shadowDist, rng);
    if (!shadow.neeSupported) {
        result.neeSupported = false;
        return result;
    }
    if (!shadow.visible) return result;

    float3 wiLocal = worldToLocal(wi, basis);
    if (materialMediumIsRefractive(state.material) && cosTheta(wiLocal) <= 0.0) {
        return result;
    }

    BSDFEval eval = evalBSDF(state, wiLocal);
    if (eval.pdf <= 0.0) return result;

    float3 fCos = eval.value * abs(dot(wi, basis.normal));
    float misWeight = powerHeuristic(pdfSolidAngle, eval.pdf);
    float3 transmittance = mediumTransmittance(medium, shadowDist);

    result.radiance = misWeight * transmittance * fCos * lightSample.emission / pdfSolidAngle;
    return result;
}

#endif
