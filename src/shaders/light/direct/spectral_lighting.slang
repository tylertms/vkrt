#ifndef VKRT_LIGHT_DIRECT_SPECTRAL_LIGHTING_SLANG
#define VKRT_LIGHT_DIRECT_SPECTRAL_LIGHTING_SLANG

#include "../../rt/queries/shadow_query.slang"
#include "./light_sampling.slang"
#include "./mis_weights.slang"

DirectLightSpectralResult sampleDirectLightSpectralSingle(
    float3 hitPoint,
    float3 geometricNormal,
    ShadingBasis basis,
    BSDFState state,
    MediumState medium,
    inout uint rng
) {
    DirectLightSpectralResult result = DirectLightSpectralResult();
    if (absCosTheta(state.wo) <= 0.0) return result;

    DirectLightSurfaceSample surfaceSample = sampleDirectLightSurface(hitPoint, geometricNormal, rng);
    if (surfaceSample.valid == 0u) return result;

    float3 shadowOffset = dot(surfaceSample.wi, geometricNormal) >= 0.0 ? geometricNormal : -geometricNormal;
    float3 shadowOrigin = hitPoint + shadowOffset * VKRT_SHADOW_ORIGIN_OFFSET;
    ShadowRayResult shadow = traceShadowRay(shadowOrigin, surfaceSample.wi, surfaceSample.shadowDistance, rng);
    if (!shadow.neeSupported()) {
        result.setNeeUnsupported();
        return result;
    }
    if (!shadow.visible()) return result;

    float3 wiLocal = worldToLocal(surfaceSample.wi, basis);
    if (materialMediumIsRefractive(state.material) && cosTheta(wiLocal) <= 0.0) return result;

    BSDFEval eval = evalSingleWavelengthBSDF(state, wiLocal);
    if (eval.pdf <= 0.0) return result;

    float spectralValue = eval.value.x * absCosTheta(wiLocal) *
                          spectralScalarFromLinearSrgb(surfaceSample.light.emission, state.wavelengthNm);
    float misWeight = powerHeuristic(surfaceSample.pdfSolidAngle, eval.pdf);
    result.spectralRadiance = misWeight * mediumTransmittance(medium, surfaceSample.shadowDistance).x * spectralValue /
                              surfaceSample.pdfSolidAngle;
    return result;
}

DirectLightHeroResult sampleDirectLightSpectralHero(
    float3 hitPoint,
    float3 geometricNormal,
    ShadingBasis basis,
    BSDFState state,
    MediumState medium,
    float4 techniquePathPdf,
    float4 wavelengthsNm,
    inout uint rng
) {
    DirectLightHeroResult result = DirectLightHeroResult();
    if (absCosTheta(state.wo) <= 0.0) return result;

    DirectLightSurfaceSample surfaceSample = sampleDirectLightSurface(hitPoint, geometricNormal, rng);
    if (surfaceSample.valid == 0u) return result;

    float3 shadowOffset = dot(surfaceSample.wi, geometricNormal) >= 0.0 ? geometricNormal : -geometricNormal;
    float3 shadowOrigin = hitPoint + shadowOffset * VKRT_SHADOW_ORIGIN_OFFSET;
    ShadowRayResult shadow = traceShadowRay(shadowOrigin, surfaceSample.wi, surfaceSample.shadowDistance, rng);
    if (!shadow.neeSupported()) {
        result.setNeeUnsupported();
        return result;
    }
    if (!shadow.visible()) return result;

    float3 wiLocal = worldToLocal(surfaceSample.wi, basis);
    if (materialMediumIsRefractive(state.material) && cosTheta(wiLocal) <= 0.0) return result;

    float4 bsdfLocalTechniquePdf = float4(0.0);
    float4 fCos = evalSpectralBSDF(state, wiLocal, wavelengthsNm, bsdfLocalTechniquePdf) * absCosTheta(wiLocal);
    float4 lightTechniquePdf = techniquePathPdf * surfaceSample.pdfSolidAngle;
    float4 bsdfTechniquePdf = techniquePathPdf * bsdfLocalTechniquePdf;
    float misWeight = computeSpectralMISWeight(lightTechniquePdf, bsdfTechniquePdf);
    if (misWeight <= 0.0) return result;

    result.spectralRadiance = misWeight * mediumSpectralTransmittance(medium, surfaceSample.shadowDistance) * fCos *
                              spectralScalarFromLinearSrgb4(surfaceSample.light.emission, wavelengthsNm) /
                              surfaceSample.pdfSolidAngle;
    return result;
}

#endif
