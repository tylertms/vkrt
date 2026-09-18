#ifndef VKRT_LIGHT_DIRECT_RGB_LIGHTING_SLANG
#define VKRT_LIGHT_DIRECT_RGB_LIGHTING_SLANG

#include "./common.slang"
#include "./mis_weights.slang"

float3 sampleDirectLightRgb(
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
    if (!light.valid()) return float3(0.0);
    if (!directLightVisible(hitPoint, geometricNormal, rayOffset, light, rng)) return float3(0.0);

    BSDFEval eval = evalBSDF(state, light.wiLocal);
    if (!any(eval.value > 0.0)) return float3(0.0);

    float misWeight = powerHeuristic(light.surface.pdfSolidAngle, bsdfContinuationAvailable ? eval.pdf : 0.0);
    float3 transmittance = mediumTransmittance(medium, light.surface.shadowDistance);
    float3 fCos = eval.value * absCosTheta(light.wiLocal);
    return misWeight * transmittance * fCos * light.surface.light.emission / light.surface.pdfSolidAngle;
}

#endif
