#include "../../core/resources.slang"
#include "../../geometry/surface.slang"
#include "../../bsdf/bsdf.slang"
#include "payload.slang"

[shader("closesthit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attr) {
    uint instanceIndex = InstanceIndex();
    MeshInfo mesh = meshInfo[instanceIndex];
    Material material = materialBuffer[mesh.materialIndex];
    float3 normal = computeSurfaceNormal(instanceIndex, PrimitiveIndex(), attr.barycentrics);
    float3 hitPoint = computeHitPoint(WorldRayOrigin(), WorldRayDirection(), RayTCurrent());
    float3 surfaceContribution = material.emissionColor * material.emissionLuminance;

    BSDFInput input = BSDFInput.make(normal, -WorldRayDirection(), material);
    BSDFSample sample = sampleBSDF(input, payload.rngState);

    if (!sample.isUsable()) {
        payload.setSurfaceHit(instanceIndex, surfaceContribution, hitPoint);
        return;
    }

    payload.setSurfaceScatter(instanceIndex, surfaceContribution, sample.weight, hitPoint + normal * 0.001, sample.wi);
}
