#ifndef VKRT_BSDF_SHEEN_SLANG
#define VKRT_BSDF_SHEEN_SLANG

#include "../common.slang"
#include "../data/sheen_ltc.slang"

// Microfiber sheen using the LTC-based multiple-scattering model used by Blender 4 Cycles.
//
// References:
// Zeltner, Burley, Chiang, 2022 - https://tizianzeltner.com/projects/Zeltner2022Practical/
// Blender Cycles sheen closure - https://github.com/blender/blender/blob/main/intern/cycles/kernel/closure/bsdf_sheen.h

static const float SHEEN_EPSILON = 1e-6;

struct SheenParams {
    ShadingBasis basis;
    float transformA;
    float transformB;
    float albedo;
};

float sheenLtcLookup(float cosThetaValue, float roughness, uint layer) {
    float2 uv = saturate(float2(cosThetaValue, roughness)) * float(SHEEN_LTC_SIZE - 1u);
    uint x0 = min(uint(uv.x), SHEEN_LTC_SIZE - 1u);
    uint y0 = min(uint(uv.y), SHEEN_LTC_SIZE - 1u);
    uint x1 = min(x0 + 1u, SHEEN_LTC_SIZE - 1u);
    uint y1 = min(y0 + 1u, SHEEN_LTC_SIZE - 1u);
    float tx = frac(uv.x);
    float ty = frac(uv.y);
    uint offset = layer * SHEEN_LTC_LAYER_SIZE;
    float e00 = SHEEN_LTC_TABLE[offset + y0 * SHEEN_LTC_SIZE + x0];
    float e10 = SHEEN_LTC_TABLE[offset + y0 * SHEEN_LTC_SIZE + x1];
    float e01 = SHEEN_LTC_TABLE[offset + y1 * SHEEN_LTC_SIZE + x0];
    float e11 = SHEEN_LTC_TABLE[offset + y1 * SHEEN_LTC_SIZE + x1];
    return lerp(lerp(e00, e10, tx), lerp(e01, e11, tx), ty);
}

SheenParams makeSheenParams(float3 wo, float sheenRoughness) {
    SheenParams params;
    params.basis = makeShadingBasis(float3(0.0, 0.0, 1.0), float4(wo, 1.0));
    params.transformA = 0.0;
    params.transformB = 0.0;
    params.albedo = 0.0;

    float cosO = cosTheta(wo);
    if (cosO <= 0.0) {
        return params;
    }

    float roughness = clamp(sheenRoughness, 1e-3, 1.0);
    params.transformA = sheenLtcLookup(cosO, roughness, 0u);
    params.transformB = sheenLtcLookup(cosO, roughness, 1u);
    params.albedo = sheenLtcLookup(cosO, roughness, 2u);
    return params;
}

float sheenDistributionValue(float3 localWi, SheenParams params) {
    float z = max(localWi.z, 0.0);
    if (z <= 0.0 || abs(params.transformA) < 1e-5 || params.albedo < 1e-5) {
        return 0.0;
    }

    float axbz = params.transformA * localWi.x + params.transformB * localWi.z;
    float lenSqr = axbz * axbz + (params.transformA * localWi.y) * (params.transformA * localWi.y) + localWi.z * localWi.z;
    if (lenSqr <= SHEEN_EPSILON) {
        return 0.0;
    }

    float scale = params.transformA / lenSqr;
    return VKRT_INV_PI * z * scale * scale;
}

float sheenDirectionalAlbedo(float cosThetaValue, float sheenRoughness) {
    return sheenLtcLookup(cosThetaValue, clamp(sheenRoughness, 1e-3, 1.0), 2u);
}

float sheenLayerAttenuation(float sheenWeight, float cosThetaValue, float sheenRoughness) {
    return saturate(1.0 - sheenWeight * sheenDirectionalAlbedo(cosThetaValue, sheenRoughness));
}

BSDFEval evalSheen(float3 sheenColor, float sheenRoughness, float3 wo, float3 wi) {
    BSDFEval eval;
    eval.value = float3(0.0);
    eval.pdf = 0.0;

    if (cosTheta(wo) <= 0.0 || cosTheta(wi) <= 0.0) {
        return eval;
    }

    SheenParams params = makeSheenParams(wo, sheenRoughness);
    float3 localWi = worldToLocal(wi, params.basis);
    float value = sheenDistributionValue(localWi, params);
    eval.value = sheenColor * (params.albedo * value);
    eval.pdf = value;
    return eval;
}

bool sampleSheen(float3 wo, float sheenRoughness, inout uint rng, out float3 wi) {
    SheenParams params = makeSheenParams(wo, sheenRoughness);
    if (abs(params.transformA) < 1e-5 || params.albedo < 1e-5) {
        wi = float3(0.0);
        return false;
    }

    float r = sqrt(rand(rng));
    float phi = 2.0 * VKRT_PI * rand(rng);
    float2 disk = r * float2(cos(phi), sin(phi));
    float diskZ = sqrt(max(1.0 - dot(disk, disk), 0.0));
    float3 localWi = normalize(float3(disk.x - diskZ * params.transformB, disk.y, diskZ * params.transformA));
    wi = localToWorld(localWi, params.basis);
    return cosTheta(wi) > 0.0;
}

#endif
