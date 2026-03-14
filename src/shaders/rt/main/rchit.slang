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

    payload.setSurfaceHit(instanceIndex, material.emissionColor * material.emissionLuminance, hitPoint);

    if (all(material.baseColor <= 0.0)) {
        return;
    }

    BSDFInput input = BSDFInput.make(normal, -WorldRayDirection(), material);
    BSDFSample sample = sampleBSDF(input, payload.rngState);

    if (!sample.isUsable()) {
        return;
    }

    payload.setScatter(sample.weight, hitPoint + normal * 0.001, sample.wi);
}
