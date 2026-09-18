#ifndef VKRT_BSDF_GGX_ENERGY_SLANG
#define VKRT_BSDF_GGX_ENERGY_SLANG

#include "../../../shared/ggx_energy.h"
#include "../../scene/resources.slang"

float ggxEnergyRoughness(float2 alpha) {
    return sqrt(sqrt(alpha.x * alpha.y));
}

float ggxEnergyAnisotropy(float2 alpha) {
    float aspect = sqrt(alpha.y / alpha.x);
    return saturate((1.0 - aspect) / (1.0 - sqrt(0.1))) * (VKRT_GGX_ENERGY_ANISOTROPY_SIZE - 1u);
}

float4 ggxEnergyCoordinate(float2 alpha, float3 wo) {
    float anisotropy = ggxEnergyAnisotropy(alpha);
    float phi = anisotropy > 0.0 && any(wo.xy != 0.0) ? atan2(abs(wo.y), abs(wo.x)) * (2.0 * VKRT_INV_PI) : 0.0;
    float cosine = abs(wo.z);
    float cosineCoordinate =
        cosine < 0.125
            ? saturate((log2(max(cosine, exp2(-24.0))) + 24.0) / 21.0) * (VKRT_GGX_ENERGY_GRAZING_SIZE - 1u)
            : (VKRT_GGX_ENERGY_GRAZING_SIZE - 1u) + (sqrt(saturate(cosine)) - sqrt(0.125)) / (1.0 - sqrt(0.125)) *
                                                        (VKRT_GGX_ENERGY_COSINE_SIZE - VKRT_GGX_ENERGY_GRAZING_SIZE);
    return float4(
        saturate(ggxEnergyRoughness(alpha)) * (VKRT_GGX_ENERGY_ROUGHNESS_SIZE - 1u),
        cosineCoordinate,
        anisotropy,
        phi * (VKRT_GGX_ENERGY_AZIMUTH_SIZE - 1u)
    );
}

float ggxEnergyDecode(uint bits) {
    return bits < 0x400u ? float(bits) * exp2(-24.0) : asfloat((bits << 13u) + 0x38000000u);
}

float ggxEnergyRead(uint index) {
    uint pair = ggxEnergyTable[index >> 1u];
    return ggxEnergyDecode((pair >> ((index & 1u) * 16u)) & 0xffffu);
}

float2 ggxEnergyRead(uint index, uint stride) {
    if (stride == 1u) return float2(ggxEnergyRead(index), 0.0);
    uint pair = ggxEnergyTable[index >> 1u];
    return float2(ggxEnergyDecode(pair & 0xffffu), ggxEnergyDecode(pair >> 16u));
}

float2 ggxEnergyLookup2D(float2 coordinate, uint offset, uint stride) {
    uint2 lower = uint2(coordinate);
    uint2 upper = min(lower + 1u, uint2(VKRT_GGX_ENERGY_ROUGHNESS_SIZE - 1u, VKRT_GGX_ENERGY_COSINE_SIZE - 1u));
    float2 fraction = frac(coordinate);
    float2 e00 = ggxEnergyRead(offset + stride * (lower.y * VKRT_GGX_ENERGY_ROUGHNESS_SIZE + lower.x), stride);
    float2 e10 = ggxEnergyRead(offset + stride * (lower.y * VKRT_GGX_ENERGY_ROUGHNESS_SIZE + upper.x), stride);
    float2 e01 = ggxEnergyRead(offset + stride * (upper.y * VKRT_GGX_ENERGY_ROUGHNESS_SIZE + lower.x), stride);
    float2 e11 = ggxEnergyRead(offset + stride * (upper.y * VKRT_GGX_ENERGY_ROUGHNESS_SIZE + upper.x), stride);
    if (stride == 1u) {
        float2 lowerEnergy = e00 * e10 / max(lerp(e10, e00, fraction.x), 1e-10);
        float2 upperEnergy = e01 * e11 / max(lerp(e11, e01, fraction.x), 1e-10);
        return lerp(lowerEnergy, upperEnergy, fraction.y);
    }
    return lerp(lerp(e00, e10, fraction.x), lerp(e01, e11, fraction.x), fraction.y);
}

float4 ggxEnergyCubicWeights(float t) {
    return float4(
        -(t - 1.0) * (t - 2.0) * (t - 3.0) / 6.0,
        t * (t - 2.0) * (t - 3.0) / 2.0,
        -t * (t - 1.0) * (t - 3.0) / 2.0,
        t * (t - 1.0) * (t - 2.0) / 6.0
    );
}

float4 ggxEnergyCubicWeights(float value, float4 knots) {
    return float4(
        (value - knots.y) * (value - knots.z) * (value - knots.w) /
            ((knots.x - knots.y) * (knots.x - knots.z) * (knots.x - knots.w)),
        (value - knots.x) * (value - knots.z) * (value - knots.w) /
            ((knots.y - knots.x) * (knots.y - knots.z) * (knots.y - knots.w)),
        (value - knots.x) * (value - knots.y) * (value - knots.w) /
            ((knots.z - knots.x) * (knots.z - knots.y) * (knots.z - knots.w)),
        (value - knots.x) * (value - knots.y) * (value - knots.z) /
            ((knots.w - knots.x) * (knots.w - knots.y) * (knots.w - knots.z))
    );
}

float2 ggxEnergyLookup(float4 coordinate, uint offset, uint stride) {
    if (coordinate.z == 0.0) return ggxEnergyLookup2D(coordinate.xy, offset, stride);
    uint2 base = uint2(clamp(
        floor(coordinate.zw) - 1.0,
        float2(0.0),
        float2(VKRT_GGX_ENERGY_ANISOTROPY_SIZE - 4u, VKRT_GGX_ENERGY_AZIMUTH_SIZE - 4u)
    ));
    float4 anisotropyWeights = ggxEnergyCubicWeights(coordinate.z - float(base.x));
    float4 azimuthWeights = ggxEnergyCubicWeights(coordinate.w - float(base.y));
    bool reciprocalAzimuth = stride == 1u && coordinate.w >= 0.5 * (VKRT_GGX_ENERGY_AZIMUTH_SIZE - 1u);
    if (reciprocalAzimuth) {
        float angleScale = 0.5 * VKRT_PI / (VKRT_GGX_ENERGY_AZIMUTH_SIZE - 1u);
        float4 knots = cos((float(base.y) + float4(0.0, 1.0, 2.0, 3.0)) * angleScale);
        float cosine = cos(coordinate.w * angleScale);
        azimuthWeights = ggxEnergyCubicWeights(cosine * cosine, knots * knots);
    }
    float2 result = float2(0.0);
    [loop] for (uint y = 0u; y < 4u; y++) {
        float2 rowEnergy = float2(0.0);
        [loop] for (uint x = 0u; x < 4u; x++) {
            uint layer = (base.y + y) * VKRT_GGX_ENERGY_ANISOTROPY_SIZE + base.x + x;
            float2 energy =
                ggxEnergyLookup2D(coordinate.xy, offset + stride * VKRT_GGX_ENERGY_ANGULAR_SIZE * layer, stride);
            rowEnergy += anisotropyWeights[x] * energy;
        }
        result += azimuthWeights[y] * (reciprocalAzimuth ? float2(1.0 / max(rowEnergy.x, 1e-12), 0.0) : rowEnergy);
    }
    return saturate(reciprocalAzimuth ? float2(1.0 / max(result.x, 1e-12), 0.0) : result);
}

float2 ggxReflectionEnergy(float2 alpha, float3 wo) {
    float4 coordinate = ggxEnergyCoordinate(alpha, wo);
    return ggxEnergyLookup(coordinate, 0u, 2u);
}

float ggxAverageEnergy(float2 alpha) {
    float2 coordinate =
        float2(saturate(ggxEnergyRoughness(alpha)) * (VKRT_GGX_ENERGY_ROUGHNESS_SIZE - 1u), ggxEnergyAnisotropy(alpha));
    uint2 lower = uint2(coordinate);
    uint2 upper = min(lower + 1u, uint2(VKRT_GGX_ENERGY_ROUGHNESS_SIZE - 1u, VKRT_GGX_ENERGY_ANISOTROPY_SIZE - 1u));
    uint offset = VKRT_GGX_ENERGY_AVERAGE_OFFSET;
    float e00 = ggxEnergyRead(offset + lower.y * VKRT_GGX_ENERGY_ROUGHNESS_SIZE + lower.x);
    float e10 = ggxEnergyRead(offset + lower.y * VKRT_GGX_ENERGY_ROUGHNESS_SIZE + upper.x);
    float e01 = ggxEnergyRead(offset + upper.y * VKRT_GGX_ENERGY_ROUGHNESS_SIZE + lower.x);
    float e11 = ggxEnergyRead(offset + upper.y * VKRT_GGX_ENERGY_ROUGHNESS_SIZE + upper.x);
    return lerp(lerp(e00, e10, frac(coordinate.x)), lerp(e01, e11, frac(coordinate.x)), frac(coordinate.y));
}

float ggxGlassEnergy(float2 alpha, float3 wo, float eta) {
    float relativeIor = eta < 1.0 ? 1.0 / eta : eta;
    float coordinate =
        saturate((log2(max(relativeIor - 1.0, exp2(-23.0))) + 23.0) / 33.0) * (VKRT_GGX_ENERGY_IOR_SIZE - 1u);
    uint lower = uint(coordinate);
    uint upper = min(lower + 1u, VKRT_GGX_ENERGY_IOR_SIZE - 1u);
    uint offset = VKRT_GGX_ENERGY_GLASS_OFFSET + (eta < 1.0 ? VKRT_GGX_ENERGY_GLASS_SIZE : 0u);
    float4 angularCoordinate = ggxEnergyCoordinate(alpha, wo);
    return lerp(
               ggxEnergyLookup(angularCoordinate, offset + lower * VKRT_GGX_ENERGY_REFLECTION_SIZE, 1u),
               ggxEnergyLookup(angularCoordinate, offset + upper * VKRT_GGX_ENERGY_REFLECTION_SIZE, 1u),
               frac(coordinate)
    )
        .x;
}

float3 ggxEnergyScale(float energy, float averageEnergy, float3 averageFresnel) {
    float missing = (1.0 - energy) / max(energy, 1e-6);
    float3 multiscatterFresnel = averageFresnel * averageEnergy / (1.0 - averageFresnel * (1.0 - averageEnergy));
    return 1.0 + multiscatterFresnel * missing;
}

float4 ggxEnergyScale4(float energy, float averageEnergy, float4 averageFresnel) {
    float missing = (1.0 - energy) / max(energy, 1e-6);
    float4 multiscatterFresnel = averageFresnel * averageEnergy / (1.0 - averageFresnel * (1.0 - averageEnergy));
    return 1.0 + multiscatterFresnel * missing;
}

#endif
