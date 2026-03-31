#include "../../rt/payloads/selection_payload.slang"
#include "../../scene/resources.slang"

[shader("closesthit")] void main(inout SelectionPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    payload.setHit();
}
