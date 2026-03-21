#include "../trace/alpha.slang"
#include "payload.slang"

[shader("anyhit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attr) {
    MeshInfo mesh = meshInfos[InstanceIndex()];
    Material material = materials[mesh.materialIndex];
    if (!alphaHitAccepted(mesh, material, PrimitiveIndex(), attr.barycentrics, payload.rng)) {
        IgnoreHit();
    }
}
