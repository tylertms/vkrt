#ifndef VKRT_RT_QUERY_SHADOW_SLANG
#define VKRT_RT_QUERY_SHADOW_SLANG

#include "../../camera/ray.slang"
#include "../../scene/resources.slang"
#include "../payloads/shadow_payload.slang"

ShadowPayload traceShadowRay(RayDesc ray, uint targetInstanceIndex, uint targetPrimitiveIndex, inout uint rng) {
    ShadowPayload payload = ShadowPayload(rng);
    payload.targetInstanceIndex = targetInstanceIndex;
    payload.targetPrimitiveIndex = targetPrimitiveIndex;

    TraceRay(
        topLevelAS,
        RAY_FLAG_NONE,
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

#endif
