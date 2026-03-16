#ifndef VKRT_TRACE_SHADOW_SLANG
#define VKRT_TRACE_SHADOW_SLANG

#include "../core/resources.slang"

struct ShadowPayload {
    uint visible;

    static ShadowPayload make() {
        ShadowPayload payload;
        payload.visible = 0u;
        return payload;
    }
};

bool traceShadowRay(float3 origin, float3 direction, float tMax) {
    ShadowPayload payload = ShadowPayload.make();

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001;
    ray.TMax = tMax;

    TraceRay(
        topLevelAS,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xffu,
        0u,
        0u,
        1u,
        ray,
        payload
    );

    return payload.visible != 0u;
}

#endif
