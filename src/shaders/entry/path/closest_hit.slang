#include "../../rt/payloads/scene_payload.slang"

[shader("closesthit")] void main(inout SceneRayPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    payload.setSurfaceHit(InstanceIndex(), PrimitiveIndex(), RayTCurrent(), attr.barycentrics);
}
