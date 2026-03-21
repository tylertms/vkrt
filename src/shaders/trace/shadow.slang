#ifndef VKRT_TRACE_SHADOW_SLANG
#define VKRT_TRACE_SHADOW_SLANG

#include "../scene/resources.slang"

struct ShadowRayResult {
    bool visible;
    bool neeSupported;
};

ShadowRayResult traceShadowRay(float3 origin, float3 direction, float tMax, inout uint rng) {
    ShadowPayload payload = ShadowPayload.make(rng);

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = VKRT_RAY_T_MIN;
    ray.TMax = tMax;

    TraceRay(
        topLevelAS,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xffu,
        1u,
        0u,
        1u,
        ray,
        payload
    );

    rng = payload.rng;

    ShadowRayResult result;
    result.visible = payload.visibility == VKRT_SHADOW_VISIBILITY_VISIBLE;
    result.neeSupported = payload.visibility != VKRT_SHADOW_VISIBILITY_UNSUPPORTED_TRANSMISSION;
    return result;
}

#endif
