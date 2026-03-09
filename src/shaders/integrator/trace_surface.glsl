#ifndef SHADER_TRACE_SURFACE_GLSL
#define SHADER_TRACE_SURFACE_GLSL

#include "utility/bindings.glsl"

void executeSurfaceTrace(vec3 origin, vec3 direction, float minDistance, float maxDistance, uint rayFlags, bool allowReorder) {
    payload.didHit = false;
    payload.hitDistance = maxDistance;

    #ifdef VKRT_USE_SER
    if (allowReorder) {
        hitObjectEXT hitObject;
        hitObjectTraceRayEXT(hitObject, topLevelAS, rayFlags, 0xFF, 0, 0, 0, origin, minDistance, direction, maxDistance, 0);
        reorderThreadEXT(hitObject);
        hitObjectExecuteShaderEXT(hitObject, 0);
        return;
    }
    #endif

    traceRayEXT(topLevelAS, rayFlags, 0xFF, 0, 0, 0, origin, minDistance, direction, maxDistance, 0);
}

#endif
