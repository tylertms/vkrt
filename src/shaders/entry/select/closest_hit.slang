#include "../../rt/payloads/selection_payload.slang"

[shader("closesthit")] void main(inout SelectionPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    payload.hasHit = 1u;
}
