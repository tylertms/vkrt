#ifndef VKRT_LIGHT_DIRECT_SPECTRAL_LIGHTING_SLANG
#define VKRT_LIGHT_DIRECT_SPECTRAL_LIGHTING_SLANG

#include "./common.slang"
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
    DirectLightSample light = sampleDirectLight(hitPoint, geometricNormal, basis, state, rng);
    if (light.neeUnsupported()) {
        result.setNeeUnsupported();
        return result;
    }
    if (!light.valid()) return result;

    BSDFEval eval = evalSingleWavelengthBSDF(state, light.wiLocal);
    if (eval.pdf <= 0.0) return result;

    float spectralValue = eval.value.x * absCosTheta(light.wiLocal) *
                          spectralScalarFromLinearSrgb(light.surface.light.emission, state.wavelengthNm);
    float misWeight = powerHeuristic(light.surface.pdfSolidAngle, eval.pdf);
    result.spectralRadiance = misWeight * mediumTransmittance(medium, light.surface.shadowDistance).x * spectralValue /
                              light.surface.pdfSolidAngle;
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
    DirectLightSample light = sampleDirectLight(hitPoint, geometricNormal, basis, state, rng);
    if (light.neeUnsupported()) {
        result.setNeeUnsupported();
        return result;
    }
    if (!light.valid()) return result;

    float4 bsdfLocalTechniquePdf = float4(0.0);
    float4 fCos =
        evalSpectralBSDF(state, light.wiLocal, wavelengthsNm, bsdfLocalTechniquePdf) * absCosTheta(light.wiLocal);
    float4 lightTechniquePdf = techniquePathPdf * light.surface.pdfSolidAngle;
    float4 bsdfTechniquePdf = techniquePathPdf * bsdfLocalTechniquePdf;
    float misWeight = computeSpectralMISWeight(lightTechniquePdf, bsdfTechniquePdf);
    if (misWeight <= 0.0) return result;

    result.spectralRadiance = misWeight * mediumSpectralTransmittance(medium, light.surface.shadowDistance) * fCos *
                              spectralScalarFromLinearSrgb4(light.surface.light.emission, wavelengthsNm) /
                              light.surface.pdfSolidAngle;
    return result;
}

#endif
