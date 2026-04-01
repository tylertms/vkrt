#ifndef VKRT_LIGHT_DIRECT_RGB_LIGHTING_SLANG
#define VKRT_LIGHT_DIRECT_RGB_LIGHTING_SLANG

#include "./common.slang"
#include "./mis_weights.slang"

DirectLightRgbResult sampleDirectLightRgb(
    float3 hitPoint,
    float3 geometricNormal,
    ShadingBasis basis,
    BSDFState state,
    MediumState medium,
    inout uint rng
) {
    DirectLightRgbResult result = DirectLightRgbResult();
    DirectLightSample light = sampleDirectLight(hitPoint, geometricNormal, basis, state, rng);
    if (light.neeUnsupported()) {
        result.setNeeUnsupported();
        return result;
    }
    if (!light.valid()) return result;

    BSDFEval eval = evalBSDF(state, light.wiLocal);
    if (eval.pdf <= 0.0) return result;

    float misWeight = powerHeuristic(light.surface.pdfSolidAngle, eval.pdf);
    float3 transmittance = mediumTransmittance(medium, light.surface.shadowDistance);
    float3 fCos = eval.value * absCosTheta(light.wiLocal);
    result.radiance = misWeight * transmittance * fCos * light.surface.light.emission / light.surface.pdfSolidAngle;
    return result;
}

#endif
