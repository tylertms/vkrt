#ifndef VKRT_BSDF_BASE_BASIS_SLANG
#define VKRT_BSDF_BASE_BASIS_SLANG

struct ShadingBasis {
    float3 tangent;
    float3 bitangent;
    float3 normal;
};

float3 makeFallbackTangent(float3 normal) {
    float3 up = abs(normal.z) < VKRT_TANGENT_PARALLEL_THRESHOLD ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    return safeNormalize(cross(up, normal));
}

ShadingBasis makeShadingBasis(float3 normal, float4 tangentData) {
    ShadingBasis basis;
    basis.normal = safeNormalize(normal);

    float3 tangent = tangentData.xyz - basis.normal * dot(tangentData.xyz, basis.normal);
    tangent =
        dot(tangent, tangent) <= VKRT_SAFE_NORMALIZE_EPS ? makeFallbackTangent(basis.normal) : safeNormalize(tangent);

    float handedness = tangentData.w < 0.0 ? -1.0 : 1.0;
    basis.tangent = tangent;
    basis.bitangent = cross(basis.normal, basis.tangent) * handedness;
    return basis;
}

float3 sanitizeShadingNormal(float3 shadingNormal, float3 geometricNormal, float3 outgoingDirection) {
    float3 normal = safeNormalize(shadingNormal);
    float3 geometric = safeNormalize(geometricNormal);
    float3 outgoing = safeNormalize(outgoingDirection);

    if (dot(normal, geometric) <= 1e-4 || dot(normal, outgoing) <= 1e-4) {
        return geometric;
    }

    return normal;
}

float3 localToWorld(float3 localDir, ShadingBasis basis) {
    return basis.tangent * localDir.x + basis.bitangent * localDir.y + basis.normal * localDir.z;
}

float3 worldToLocal(float3 worldDir, ShadingBasis basis) {
    return float3(dot(worldDir, basis.tangent), dot(worldDir, basis.bitangent), dot(worldDir, basis.normal));
}

#endif
