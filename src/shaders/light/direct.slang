#ifndef VKRT_LIGHTING_DIRECT_SLANG
#define VKRT_LIGHTING_DIRECT_SLANG

// References:
// MIS power heuristic: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency
// Direct lighting / NEE: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer
// Alias method: Schwarz, 2011 - https://www.keithschwarz.com/darts-dice-coins/
// Hero wavelengths: Wilkie et al., 2014 - https://cgg.mff.cuni.cz/publications/hero-wavelength-spectral-sampling/

#include "../bsdf/bsdf.slang"
#include "../sample/sampling.slang"
#include "../sample/wavelength.slang"
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
    float4 spectralRadiance;
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
    float lightPdfSolidAngle = lightPdfAreaToSolidAngle(lightPdfArea, geometricNormal, rayDirection, hitDistance);
    if (lightPdfSolidAngle <= 0.0) return 1.0;
    return powerHeuristic(bsdfPdf, lightPdfSolidAngle);
}

float lightPdfAreaToSolidAngle(float lightPdfArea, float3 geometricNormal, float3 rayDirection, float hitDistance) {
    if (lightPdfArea <= 0.0) return 0.0;
    float distSq = hitDistance * hitDistance;
    if (distSq <= 0.0) return 0.0;

    float cosLight = abs(dot(rayDirection, geometricNormal));
    if (cosLight <= 0.0) return 0.0;

    return lightPdfArea * distSq / cosLight;
}

float computeSpectralMISWeight(float4 sampledTechniquePdf, float4 alternateTechniquePdf) {
    float denominator = dot(sampledTechniquePdf, float4(1.0)) + dot(alternateTechniquePdf, float4(1.0));
    return denominator > 0.0 ? sampledTechniquePdf.x / denominator : 0.0;
}

float computeSpectralEmitterMISWeight(
    float4 previousVertexTechniquePathPdf,
    float4 previousBsdfTechniquePdf,
    float lightPdfSolidAngle
) {
    float4 bsdfTechniquePdf = previousVertexTechniquePathPdf * previousBsdfTechniquePdf;
    float4 lightTechniquePdf = previousVertexTechniquePathPdf * lightPdfSolidAngle;
    return computeSpectralMISWeight(bsdfTechniquePdf, lightTechniquePdf);
}

DirectLightResult sampleDirectLight(
    float3 hitPoint,
    float3 geometricNormal,
    ShadingBasis basis,
    BSDFState state,
    MediumState medium,
    float4 techniquePathPdf,
    float4 wavelengthsNm,
    bool heroWavelengthMode,
    inout uint rng
) {
    DirectLightResult result;
    result.radiance = float3(0.0);
    result.spectralRadiance = float4(0.0);
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

    float absCosWi = absCosTheta(wiLocal);
    if (state.spectralMode != 0u) {
        if (!heroWavelengthMode) {
            BSDFEval eval = evalSingleWavelengthBSDF(state, wiLocal);
            if (eval.pdf <= 0.0) return result;

            float spectralValue =
                eval.value.x * absCosWi * spectralScalarFromLinearSrgb(lightSample.emission, state.wavelengthNm);
            float misWeight = powerHeuristic(pdfSolidAngle, eval.pdf);
            result.spectralRadiance.x =
                misWeight * mediumTransmittance(medium, shadowDist).x * spectralValue / pdfSolidAngle;
            return result;
        }

        float4 bsdfLocalTechniquePdf = float4(0.0);
        float4 fCos = evalSpectralBSDF(state, wiLocal, wavelengthsNm, bsdfLocalTechniquePdf) * absCosWi;
        float4 lightTechniquePdf = techniquePathPdf * pdfSolidAngle;
        float4 bsdfTechniquePdf = techniquePathPdf * bsdfLocalTechniquePdf;
        float misWeight = computeSpectralMISWeight(lightTechniquePdf, bsdfTechniquePdf);
        if (misWeight <= 0.0) return result;

        result.spectralRadiance = misWeight * mediumSpectralTransmittance(medium, shadowDist) * fCos *
                                  spectralScalarFromLinearSrgb4(lightSample.emission, wavelengthsNm) / pdfSolidAngle;
        return result;
    }

    float3 transmittance = mediumTransmittance(medium, shadowDist);
    BSDFEval eval = evalBSDF(state, wiLocal);
    if (eval.pdf <= 0.0) return result;

    float3 fCos = eval.value * absCosWi;
    float misWeight = powerHeuristic(pdfSolidAngle, eval.pdf);

    result.radiance = misWeight * transmittance * fCos * lightSample.emission / pdfSolidAngle;
    return result;
}

#endif
