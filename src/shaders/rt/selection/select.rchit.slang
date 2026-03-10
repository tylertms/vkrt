#include "../../core/resources.slang"

[shader("closesthit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attr) {
    payload.radiance = float3(0.0);
    payload.normal = float3(0.0);
    payload.didHit = 1u;
    payload.instanceIndex = InstanceIndex();
    payload.hitDistance = RayTCurrent();
}
