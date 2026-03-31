#ifndef VKRT_TRACE_SHADOW_SLANG
#define VKRT_TRACE_SHADOW_SLANG

#include "../path/payload.slang"
#include "../scene/resources.slang"
#include "layout.slang"

struct ShadowRayResult {
    uint flags;

    __init() {
        flags = 0u;
    }

    bool visible() {
        return (flags & 1u) != 0u;
    }

    bool neeSupported() {
        return (flags & 2u) != 0u;
    }
};

ShadowRayResult traceShadowRay(float3 origin, float3 direction, float tMax, inout uint rng) {
    ShadowPayload payload = ShadowPayload(rng);

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = VKRT_RAY_T_MIN;
    ray.TMax = tMax;

    TraceRay(
        topLevelAS,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xffu,
        kShadowRaySbtOffset,
        kSceneHitGroupStride,
        kShadowMissIndex,
        ray,
        payload
    );

    rng = payload.rng;

    ShadowRayResult result = ShadowRayResult();
    if (payload.visibility == VKRT_SHADOW_VISIBILITY_VISIBLE) {
        result.flags |= 1u;
    }
    if (payload.visibility != VKRT_SHADOW_VISIBILITY_UNSUPPORTED_TRANSMISSION) {
        result.flags |= 2u;
    }
    return result;
}

#endif
