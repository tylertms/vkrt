#ifndef VKRT_TRACE_SCENE_SLANG
#define VKRT_TRACE_SCENE_SLANG

#include "../scene/resources.slang"
#include "../path/payload.slang"

static const uint kSceneRayMask = 0xffu;
static const uint kSceneRayFlags = RAY_FLAG_NONE;

void traceSceneRay(inout Payload payload, RayDesc ray, uint depth, uint coherenceHint, uint coherenceHintBits) {
#ifdef VKRT_SER_ENABLED
    if (depth > 0u) {
        HitObject hitObject = HitObject::TraceRay(topLevelAS, kSceneRayFlags, kSceneRayMask, 0u, 0u, 0u, ray, payload);
        ReorderThread(hitObject, coherenceHint, coherenceHintBits);
        HitObject::Invoke(topLevelAS, hitObject, payload);
        return;
    }
#endif
    TraceRay(topLevelAS, kSceneRayFlags, kSceneRayMask, 0u, 0u, 0u, ray, payload);
}

#endif
