#ifndef VKRT_RT_QUERY_SCENE_SLANG
#define VKRT_RT_QUERY_SCENE_SLANG

#include "../../scene/resources.slang"
#include "../layout.slang"
#include "../payloads/scene_payload.slang"

static const uint VKRT_SCENE_RAY_MASK = 0xffu;
static const uint VKRT_SCENE_RAY_FLAGS = RAY_FLAG_NONE;

SceneRayPayload traceSceneRay(RayDesc ray, uint depth, uint coherenceHint, uint coherenceHintBits, inout uint rng) {
    SceneRayPayload payload = SceneRayPayload(rng);
#ifdef VKRT_SER_ENABLED
    if (depth > 0u) {
        HitObject hitObject = HitObject::TraceRay(
            topLevelAS,
            VKRT_SCENE_RAY_FLAGS,
            VKRT_SCENE_RAY_MASK,
            VKRT_MAIN_SCENE_RAY_SBT_OFFSET,
            VKRT_SCENE_HIT_GROUP_STRIDE,
            VKRT_MAIN_MISS_INDEX,
            ray,
            payload
        );
        ReorderThread(hitObject, coherenceHint, coherenceHintBits);
        HitObject::Invoke(topLevelAS, hitObject, payload);
        rng = payload.rng;
        return payload;
    }
#endif
    TraceRay(
        topLevelAS,
        VKRT_SCENE_RAY_FLAGS,
        VKRT_SCENE_RAY_MASK,
        VKRT_MAIN_SCENE_RAY_SBT_OFFSET,
        VKRT_SCENE_HIT_GROUP_STRIDE,
        VKRT_MAIN_MISS_INDEX,
        ray,
        payload
    );
    rng = payload.rng;
    return payload;
}

#endif
