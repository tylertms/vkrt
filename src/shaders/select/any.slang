#include "../trace/alpha.slang"
#include "payload.slang"

[shader("anyhit")]
void main(inout SelectionPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    MeshInfo mesh = meshInfos[InstanceIndex()];
    Material material = materials[mesh.materialIndex];
    if (!selectionAlphaHitAccepted(mesh, material, PrimitiveIndex(), attr.barycentrics)) {
        IgnoreHit();
    }
}
