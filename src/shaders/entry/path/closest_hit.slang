#include "../../geometry/surface/interpolation.slang"
#include "../../geometry/surface/ray_origin.slang"
#include "../../rt/payloads/scene_payload.slang"

[shader("closesthit")] void main(inout SceneRayPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    payload.setSurfaceHit(InstanceIndex(), PrimitiveIndex(), RayTCurrent(), attr.barycentrics);
    ShaderVertex vertex0, vertex1, vertex2;
    loadTriangleVertices(meshInfos[InstanceIndex()], PrimitiveIndex(), vertex0, vertex1, vertex2);
    SurfaceRayOrigin origin = makeSurfaceRayOrigin(
        vertex0.position.xyz,
        vertex1.position.xyz - vertex0.position.xyz,
        vertex2.position.xyz - vertex0.position.xyz,
        attr.barycentrics,
        ObjectToWorld3x4(),
        WorldToObject3x4()
    );
    payload.position = origin.position;
    payload.normal = origin.normal;
    payload.rayOffset = origin.offset;
}
