#ifndef VKRT_TRACE_SCENE_SLANG
#define VKRT_TRACE_SCENE_SLANG

#include "../core/resources.slang"
#include "../rt/main/payload.slang"

static const uint kSceneRayMask = 0xffu;
static const uint kSceneRayFlags = RAY_FLAG_NONE;

void traceSceneRay(inout Payload payload, RayDesc ray, uint depth) {
/*
#ifdef VKRT_SER_ENABLED
    if (depth > 0u) {
        HitObject hitObject = HitObject::TraceRay(topLevelAS, kSceneRayFlags, kSceneRayMask, 0u, 0u, 0u, ray, payload);
        ReorderThread(hitObject);
        HitObject::Invoke(topLevelAS, hitObject, payload);
        return;
    }
#endif
*/
    TraceRay(topLevelAS, kSceneRayFlags, kSceneRayMask, 0u, 0u, 0u, ray, payload);
}

#endif
