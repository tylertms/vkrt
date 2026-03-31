#include "../scene/resources.slang"
#include "payload.slang"

[shader("closesthit")] void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attr) {
    uint instanceIndex = InstanceIndex();
    payload.setSurfaceHit(instanceIndex, PrimitiveIndex(), RayTCurrent(), attr.barycentrics);
}
