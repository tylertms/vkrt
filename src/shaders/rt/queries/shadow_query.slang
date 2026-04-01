#ifndef VKRT_RT_QUERY_SHADOW_SLANG
#define VKRT_RT_QUERY_SHADOW_SLANG

#include "../../camera/ray.slang"
#include "../../scene/resources.slang"
#include "../layout.slang"
#include "../payloads/shadow_payload.slang"

ShadowPayload traceShadowRay(RayDesc ray, inout uint rng) {
    ShadowPayload payload = ShadowPayload(rng);

    TraceRay(
        topLevelAS,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xffu,
        VKRT_SHADOW_RAY_SBT_OFFSET,
        VKRT_SCENE_HIT_GROUP_STRIDE,
        VKRT_SHADOW_MISS_INDEX,
        ray,
        payload
    );

    rng = payload.rng;
    return payload;
}

ShadowPayload traceShadowRay(float3 origin, float3 direction, float tMax, inout uint rng) {
    return traceShadowRay(makeRay(origin, direction, VKRT_RAY_T_MIN, tMax), rng);
}

#endif
