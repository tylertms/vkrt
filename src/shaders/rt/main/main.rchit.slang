#include "../../core/resources.slang"
#include "../../integrator/surface.slang"

[shader("closesthit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attr) {
    uint instanceIndex = InstanceIndex();
    float3 worldNormal = computeSurfaceNormal(instanceIndex, PrimitiveIndex(), attr.barycentrics);

    float distance = RayTCurrent();
    payload.radiance = worldNormal * 0.5 + 0.5;
    payload.normal = worldNormal;
    payload.didHit = 1u;
    payload.instanceIndex = instanceIndex;
    payload.hitDistance = distance;
}
