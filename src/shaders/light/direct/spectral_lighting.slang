#ifndef VKRT_LIGHT_DIRECT_SPECTRAL_LIGHTING_SLANG
#define VKRT_LIGHT_DIRECT_SPECTRAL_LIGHTING_SLANG

#include "./common.slang"
#include "./mis_weights.slang"

float sampleDirectLightSpectralSingle(
    float3 hitPoint,
    float3 geometricNormal,
    float rayOffset,
    ShadingBasis basis,
    BSDFState state,
    MediumState medium,
    bool bsdfContinuationAvailable,
    inout uint rng
) {
    DirectLightSample light = sampleDirectLight(hitPoint, basis, state, rng);
    if (!light.valid()) return 0.0;
    if (!directLightVisible(hitPoint, geometricNormal, rayOffset, light, rng)) return 0.0;

    BSDFEval eval = evalSingleWavelengthBSDF(state, light.wiLocal);
    if (eval.value.x <= 0.0) return 0.0;

    float spectralValue = eval.value.x * absCosTheta(light.wiLocal) *
                          spectralScalarFromLinearSrgb(light.surface.light.emission, state.wavelengthNm);
    float misWeight = powerHeuristic(light.surface.pdfSolidAngle, bsdfContinuationAvailable ? eval.pdf : 0.0);
    return misWeight * mediumTransmittance(medium, light.surface.shadowDistance).x * spectralValue /
           light.surface.pdfSolidAngle;
}

float4 sampleDirectLightSpectralHero(
    float3 hitPoint,
    float3 geometricNormal,
    float rayOffset,
    ShadingBasis basis,
    BSDFState state,
    MediumState medium,
    float4 techniquePathPdf,
    float4 wavelengthsNm,
    bool bsdfContinuationAvailable,
    inout uint rng
) {
    DirectLightSample light = sampleDirectLight(hitPoint, basis, state, rng);
    if (!light.valid()) return float4(0.0);
    if (!directLightVisible(hitPoint, geometricNormal, rayOffset, light, rng)) return float4(0.0);

    float4 bsdfLocalTechniquePdf = float4(0.0);
    float4 fCos =
        evalSpectralBSDF(state, light.wiLocal, wavelengthsNm, bsdfLocalTechniquePdf) * absCosTheta(light.wiLocal);
    if (!any(fCos > 0.0)) return float4(0.0);
    float4 lightTechniquePdf = techniquePathPdf * light.surface.pdfSolidAngle;
    float4 bsdfTechniquePdf = bsdfContinuationAvailable ? techniquePathPdf * bsdfLocalTechniquePdf : float4(0.0);
    float misWeight = computeSpectralMISWeight(lightTechniquePdf, bsdfTechniquePdf);
    if (misWeight <= 0.0) return float4(0.0);

    return misWeight * mediumSpectralTransmittance(medium, light.surface.shadowDistance) * fCos *
           spectralScalarFromLinearSrgb4(light.surface.light.emission, wavelengthsNm) / light.surface.pdfSolidAngle;
}

#endif
