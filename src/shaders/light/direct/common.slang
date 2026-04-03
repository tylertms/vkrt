#ifndef VKRT_LIGHT_DIRECT_COMMON_SLANG
#define VKRT_LIGHT_DIRECT_COMMON_SLANG

#include "../../rt/queries/shadow_query.slang"
#include "./light_sampling.slang"

static const uint VKRT_DIRECT_LIGHT_VALID = 1u << 0;
static const uint VKRT_DIRECT_LIGHT_NEE_UNSUPPORTED = 1u << 1;

struct DirectLightSample {
    DirectLightSurfaceSample surface;
    float3 wiLocal = float3(0.0);
    uint flags = 0u;

    bool valid() {
        return (flags & VKRT_DIRECT_LIGHT_VALID) != 0u;
    }

    bool neeUnsupported() {
        return (flags & VKRT_DIRECT_LIGHT_NEE_UNSUPPORTED) != 0u;
    }
};

DirectLightSample sampleDirectLight(
    float3 hitPoint,
    float3 geometricNormal,
    ShadingBasis basis,
    BSDFState state,
    inout uint rng
) {
    DirectLightSample light = DirectLightSample();
    if (cosTheta(state.wo) <= 0.0) return light;

    light.surface = sampleDirectLightSurface(hitPoint, geometricNormal, rng);
    if (light.surface.valid == 0u) return light;

    float3 shadowOffset = dot(light.surface.wi, geometricNormal) >= 0.0 ? geometricNormal : -geometricNormal;
    float3 shadowOrigin = hitPoint + shadowOffset * VKRT_SHADOW_ORIGIN_OFFSET;
    ShadowPayload shadow = traceShadowRay(shadowOrigin, light.surface.wi, light.surface.shadowDistance, rng);
    if (!shadow.neeSupported()) {
        light.flags |= VKRT_DIRECT_LIGHT_NEE_UNSUPPORTED;
        return light;
    }
    if (!shadow.visible()) return light;

    light.wiLocal = worldToLocal(light.surface.wi, basis);
    if (materialMediumIsRefractive(state.material) && cosTheta(light.wiLocal) <= 0.0) {
        return light;
    }

    light.flags |= VKRT_DIRECT_LIGHT_VALID;
    return light;
}

#endif
