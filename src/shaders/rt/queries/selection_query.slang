#ifndef VKRT_RT_QUERY_SELECTION_SLANG
#define VKRT_RT_QUERY_SELECTION_SLANG

#include "../../scene/resources.slang"
#include "../payloads/selection_payload.slang"

SelectionPayload traceSelectionRay(RayDesc ray) {
    SelectionPayload payload = SelectionPayload();
    TraceRay(selectionTopLevelAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xffu, 0u, 0u, 0u, ray, payload);
    return payload;
}

#endif
