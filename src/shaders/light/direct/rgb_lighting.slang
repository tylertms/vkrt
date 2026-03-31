#ifndef VKRT_LIGHT_DIRECT_RGB_LIGHTING_SLANG
#define VKRT_LIGHT_DIRECT_RGB_LIGHTING_SLANG

#include "../../rt/queries/shadow_query.slang"
#include "./light_sampling.slang"
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

    BSDFEval eval = evalBSDF(state, wiLocal);
    if (eval.pdf <= 0.0) return result;

    float misWeight = powerHeuristic(surfaceSample.pdfSolidAngle, eval.pdf);
    float3 transmittance = mediumTransmittance(medium, surfaceSample.shadowDistance);
    float3 fCos = eval.value * absCosTheta(wiLocal);
    result.radiance = misWeight * transmittance * fCos * surfaceSample.light.emission / surfaceSample.pdfSolidAngle;
    return result;
}

#endif
