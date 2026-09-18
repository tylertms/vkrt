#ifndef VKRT_LIGHT_DIRECT_COMMON_SLANG
#define VKRT_LIGHT_DIRECT_COMMON_SLANG

#include "../../geometry/surface/ray_origin.slang"
#include "../../rt/queries/shadow_query.slang"
#include "./light_sampling.slang"

static const uint VKRT_DIRECT_LIGHT_VALID = 1u << 0;

struct DirectLightSample {
    DirectLightSurfaceSample surface;
    float3 wiLocal = float3(0.0);
    uint flags = 0u;

    bool valid() {
        return (flags & VKRT_DIRECT_LIGHT_VALID) != 0u;
    }
};

DirectLightSample sampleDirectLight(float3 hitPoint, ShadingBasis basis, BSDFState state, inout uint rng) {
    DirectLightSample light = DirectLightSample();
    if (cosTheta(state.wo) <= 0.0) return light;

    light.surface = sampleDirectLightSurface(hitPoint, rng);
    if (light.surface.valid == 0u) return light;

    light.wiLocal = worldToLocal(light.surface.wi, basis);
    if (cosTheta(light.wiLocal) <= 0.0 || !bsdfDirectionIsValid(state, light.wiLocal)) return light;

    light.flags |= VKRT_DIRECT_LIGHT_VALID;
    return light;
}

bool directLightVisible(
    float3 hitPoint,
    float3 geometricNormal,
    float rayOffset,
    DirectLightSample light,
    inout uint rng
) {
    float3 shadowOrigin = offsetSurfaceRayOrigin(hitPoint, geometricNormal, rayOffset);
    float3 toLight = light.surface.light.position - shadowOrigin;
    if (dot(toLight, toLight) == 0.0) return false;
    return traceShadowRay(
               makeRay(shadowOrigin, toLight, 0.0, 1.0),
               light.surface.light.instanceIndex,
               light.surface.light.primitiveIndex,
               rng
    )
        .visible();
}

#endif
