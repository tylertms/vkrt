#ifndef VKRT_LIGHTING_DIRECT_SLANG
#define VKRT_LIGHTING_DIRECT_SLANG

// References:
// MIS power heuristic: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency
// Direct lighting / NEE: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer
// Alias method: Schwarz, 2011 - https://www.keithschwarz.com/darts-dice-coins/

#include "../core/resources.slang"
#include "../trace/shadow.slang"
#include "../utility/sampling.slang"
#include "../bsdf/bsdf.slang"

struct LightSample {
    float3 position;
    float3 normal;
    float3 emission;
    float pdf;
    bool valid;
};

LightSample sampleEmissiveLight(inout uint rng) {
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

float computeBSDFEmitterMISWeight(float bsdfPdf, float lightPdfArea, float3 geometricNormal, float3 rayDirection, float hitDistance) {
    if (lightPdfArea <= 0.0) return 1.0;

    float dirLenSq = dot(rayDirection, rayDirection);
    if (dirLenSq <= 0.0) return 1.0;

    float distSq = hitDistance * hitDistance * dirLenSq;
    if (distSq <= 0.0) return 1.0;

    float cosLight = abs(dot(rayDirection * rsqrt(dirLenSq), geometricNormal));
    if (cosLight <= 0.0) return 0.0;

    float lightPdfSolidAngle = lightPdfArea * distSq / cosLight;
    return powerHeuristic(bsdfPdf, lightPdfSolidAngle);
}

float3 evaluateDirectLight(float3 hitPoint, float3 geometricNormal, ShadingBasis basis, BSDFState state, inout uint rng) {
    if (absCosTheta(state.wo) <= 0.0) return float3(0.0);

    LightSample ls = sampleEmissiveLight(rng);
    if (!ls.valid) return float3(0.0);

    float3 toLight = ls.position - hitPoint;
    float distSq = dot(toLight, toLight);
    if (distSq <= 0.0) return float3(0.0);

    float invDist = rsqrt(distSq);
    float dist = distSq * invDist;
    float3 wi = toLight * invDist;

    float cosLight = abs(dot(wi, ls.normal));
    if (cosLight <= 0.0) return float3(0.0);

    float pdfSolidAngle = ls.pdf * distSq / cosLight;
    if (pdfSolidAngle <= 0.0) return float3(0.0);

    float3 shadowOrigin = hitPoint + geometricNormal * 0.001;
    float shadowDist = dist - 0.002;
    if (shadowDist <= 0.0) return float3(0.0);

    if (!traceShadowRay(shadowOrigin, wi, shadowDist)) return float3(0.0);

    float3 wiLocal = worldToLocal(wi, basis);
    BSDFEval eval = evalBSDF(state, wiLocal);
    if (eval.pdf <= 0.0) return float3(0.0);

    float3 fCos = eval.value * abs(dot(wi, basis.normal));
    float misWeight = powerHeuristic(pdfSolidAngle, eval.pdf);

    return misWeight * fCos * ls.emission / pdfSolidAngle;
}

#endif
