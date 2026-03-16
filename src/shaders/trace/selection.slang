#ifndef VKRT_TRACE_SELECTION_SLANG
#define VKRT_TRACE_SELECTION_SLANG

#include "../core/resources.slang"
#include "../rt/selection/payload.slang"

void traceSelectionRay(inout SelectionPayload payload, RayDesc ray) {
    TraceRay(
        selectionTopLevelAS,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xffu,
        0u,
        0u,
        0u,
        ray,
        payload
    );
}

#endif
