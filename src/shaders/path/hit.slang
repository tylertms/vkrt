#include "scene/resources.slang"
#include "geometry/surface.slang"
#include "geometry/packing.slang"
#include "payload.slang"

[shader("closesthit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attr) {
    uint instanceIndex = InstanceIndex();
    MeshInfo mesh = meshInfos[instanceIndex];

    SurfaceHit hit = evaluateSurfaceHit(
        mesh,
        PrimitiveIndex(),
        attr.barycentrics,
        ObjectToWorld3x4(),
        WorldToObject3x4(),
        WorldRayDirection()
    );

    payload.setSurfaceHit(
        instanceIndex,
        hit.materialIndex,
        hit.frontFace,
        RayTCurrent(),
        packOctNormal(hit.shadingNormal),
        packOctNormal(hit.geometricNormal),
        hit.tangent
    );
}
