#include "../../rt/alpha_test.slang"
#include "../../rt/payloads/shadow_payload.slang"

[shader("anyhit")] void main(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    MeshInfo mesh = meshInfos[InstanceIndex()];
    Material material = materials[mesh.materialIndex];
    if (!alphaHitAccepted(mesh, material, PrimitiveIndex(), attr.barycentrics, payload.rng)) {
        IgnoreHit();
    }
}
