#ifndef VKRT_TRACE_SCENE_SLANG
#define VKRT_TRACE_SCENE_SLANG

#include "../core/resources.slang"
#include "../integrator/bounce.slang"
#include "../rt/main/payload.slang"

static const uint kSceneRayMask = 0xffu;
static const uint kSceneRayFlags = RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES;

RayDesc makePathTraceRay(in PathPayload payload) {
    return makeRay(payload.rayOrigin, payload.rayDirection, kPathRayMinDistance, kPathRayMaxDistance);
}

void configureSerHitObject(inout HitObject hitObject) {
    if (!hitObject.IsHit()) {
        return;
    }

    uint instanceIndex = hitObject.GetInstanceIndex();
    MeshInfo mesh = meshInfo[instanceIndex];
    Material material = materialBuffer[mesh.materialIndex];
    hitObject.SetShaderTableIndex(materialSerShaderTableIndex(material));
}

void traceSceneRay(inout PathPayload payload) {
    RayDesc ray = makePathTraceRay(payload);

#ifdef VKRT_SER_ENABLED
    if (payload.depth > 0u) {
        HitObject hitObject = HitObject::TraceRay(topLevelAS, kSceneRayFlags, kSceneRayMask, 0u, 0u, 0u, ray, payload);
        configureSerHitObject(hitObject);
        ReorderThread(hitObject);
        HitObject::Invoke(topLevelAS, hitObject, payload);
        return;
    }
#endif

    TraceRay(topLevelAS, kSceneRayFlags, kSceneRayMask, 0u, 0u, 0u, ray, payload);
}

#endif
