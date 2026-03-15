#include "../../core/resources.slang"
#include "../../geometry/surface.slang"
#include "../../bsdf/bsdf.slang"
#include "payload.slang"

[shader("closesthit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attr) {
    uint instanceIndex = InstanceIndex();
    MeshInfo mesh = meshInfo[instanceIndex];
    Material material = materialBuffer[mesh.materialIndex];
    float3 rawGeometricNormal = computeGeometricNormalUnoriented(instanceIndex, PrimitiveIndex());
    uint frontFace = dot(rawGeometricNormal, WorldRayDirection()) < 0.0 ? 1u : 0u;
    float3 normal = computeSurfaceNormal(instanceIndex, PrimitiveIndex(), attr.barycentrics);
    float4 tangent = computeSurfaceTangent(instanceIndex, PrimitiveIndex(), attr.barycentrics);
    float3 hitPoint = computeHitPoint(WorldRayOrigin(), WorldRayDirection(), RayTCurrent());
    float3 surfaceContribution = material.emissionColor * material.emissionLuminance;

    BSDFInput input = BSDFInput.make(normal, tangent, -WorldRayDirection(), frontFace, material);
    BSDFSample sample = sampleBSDF(input, payload.rngState);

    if (!sample.isUsable()) {
        payload.setSurfaceHit(instanceIndex, surfaceContribution, hitPoint);
        return;
    }

    float3 geometricNormal = frontFace != 0u ? rawGeometricNormal : -rawGeometricNormal;
    float3 offsetDir = sample.isTransmission != 0u ? -geometricNormal : geometricNormal;
    payload.setSurfaceScatter(instanceIndex, surfaceContribution, sample.weight, hitPoint + offsetDir * 0.001, sample.wi);
}
