#include "../../core/resources.slang"
#include "payload.slang"

[shader("closesthit")]
void main(inout SelectionPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    payload.setHit();
}
