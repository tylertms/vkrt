#ifndef VKRT_BSDF_GGX_SLANG
#define VKRT_BSDF_GGX_SLANG

#include "../base.slang"
#include "./ggx_energy.slang"

static const float GGX_MIN_ALPHA = 1e-3;
static const float GGX_EPSILON = 1e-6;

struct GGXParams {
    float2 alpha;
    float projectedWoLength;
    float energy;
    float averageEnergy;
    float schlickEnergy;
    float4 glassEnergyScale;
};

float2 ggxAlpha(BSDFMaterial material) {
    float roughness = max(material.roughness, GGX_MIN_ALPHA);
    float aspect = sqrt(max(1.0 - 0.9 * material.anisotropic, GGX_MIN_ALPHA));
    float a = roughness * roughness;
    return max(float2(a / aspect, a * aspect), float2(GGX_MIN_ALPHA));
}

float ggxDistribution(float3 m, float2 alpha) {
    if (m.z <= 0.0) return 0.0;
    float c = (m.x * m.x) / (alpha.x * alpha.x) + (m.y * m.y) / (alpha.y * alpha.y) + m.z * m.z;
    return 1.0 / (VKRT_PI * alpha.x * alpha.y * c * c);
}

float ggxMasking(float3 wo, float3 wi, float2 alpha) {
    float cosO = absCosTheta(wo);
    float cosI = absCosTheta(wi);
    float denominator = cosO * ggxProjectedLength(wi, alpha) + cosI * ggxProjectedLength(wo, alpha);
    return denominator > 0.0 ? 2.0 * cosO * cosI / denominator : 0.0;
}

float ggxProjectedLength(float3 wo, float2 alpha) {
    float2 stretchedXY = alpha * wo.xy;
    return sqrt(dot(stretchedXY, stretchedXY) + wo.z * wo.z);
}

GGXParams makeGGXParams(BSDFMaterial material, float3 wo) {
    GGXParams params;
    params.alpha = ggxAlpha(material);
    params.projectedWoLength = ggxProjectedLength(wo, params.alpha);
    params.glassEnergyScale = float4(1.0);
    params.energy = 1.0;
    params.schlickEnergy = 0.0;
    params.averageEnergy = 1.0;
    if (material.metallic > 0.0 || (material.transmission < 1.0 && interfaceIor(material) > 1.0)) {
        float2 energy = ggxReflectionEnergy(params.alpha, wo);
        params.energy = energy.x;
        params.schlickEnergy = energy.y;
        params.averageEnergy = ggxAverageEnergy(params.alpha);
    }
    return params;
}

bool ggxHalfVector(float3 wo, float3 wi, out float3 m, out float woDotM) {
    float3 h = wo + wi;
    float h2 = dot(h, h);
    if (h2 <= 0.0) {
        m = float3(0.0);
        woDotM = 0.0;
        return false;
    }

    m = h * rsqrt(h2);
    woDotM = dot(wo, m);
    return woDotM > 0.0;
}

float ggxVisibleNormalPdf(float3 wo, float3 m, GGXParams params) {
    float woDotM = dot(wo, m);
    if (cosTheta(wo) <= 0.0 || m.z <= 0.0 || woDotM <= 0.0) {
        return 0.0;
    }

    float Dm = ggxDistribution(m, params.alpha);
    return 2.0 * Dm * woDotM / (cosTheta(wo) + params.projectedWoLength);
}

float ggxReflectionPdf(float3 wo, float3 m, GGXParams params) {
    if (cosTheta(wo) <= 0.0 || m.z <= 0.0 || dot(wo, m) <= 0.0) return 0.0;
    return ggxDistribution(m, params.alpha) / (2.0 * (cosTheta(wo) + params.projectedWoLength));
}

float3 ggxConductorFresnel(BSDFMaterial material, float cosTheta, float lambdaNm, uint spectralMode) {
    float3 fresnelColor = materialHasConductor(material)
                            ? fresnelConductor(cosTheta, max(material.eta, float3(1e-3)), max(material.k, float3(0.0)))
                            : fresnelSchlick(cosTheta, material.baseColor);
    return spectralMode != 0u ? spectralScalarFromLinearSrgb(saturate(fresnelColor), lambdaNm).xxx : fresnelColor;
}

float4 ggxConductorFresnel4(BSDFMaterial material, float cosTheta, float4 wavelengthsNm) {
    float3 fresnelColor = materialHasConductor(material)
                            ? fresnelConductor(cosTheta, max(material.eta, float3(1e-3)), max(material.k, float3(0.0)))
                            : fresnelSchlick(cosTheta, material.baseColor);
    return spectralScalarFromLinearSrgb4(saturate(fresnelColor), wavelengthsNm);
}

float3 ggxConductorAverageFresnel(BSDFMaterial material) {
    if (!materialHasConductor(material)) return lerp(material.baseColor, float3(1.0), 1.0 / 21.0);
    float3 eta = max(material.eta, float3(1e-3));
    float3 k = max(material.k, float3(0.0));
    float3 f0 = fresnelConductor(1.0, eta, k);
    float3 f82 = fresnelConductor(1.0 / 7.0, eta, k);
    float edgeWeight = pow(6.0 / 7.0, 5.0);
    float3 correction = (lerp(f0, float3(1.0), edgeWeight) - f82) * (7.0 / ((6.0 / 7.0) * edgeWeight));
    return saturate(lerp(f0, float3(1.0), 1.0 / 21.0) - correction / 126.0);
}

BSDFEval evalGGXFromHalfVector(
    BSDFMaterial material,
    float3 wo,
    float3 wi,
    GGXParams params,
    float3 m,
    float woDotM,
    float lambdaNm,
    uint spectralMode
) {
    float Dm = ggxDistribution(m, params.alpha);
    float G = ggxMasking(wo, wi, params.alpha);
    float3 F = ggxConductorFresnel(material, woDotM, lambdaNm, spectralMode);
    float3 averageFresnel = ggxConductorAverageFresnel(material);
    if (spectralMode != 0u) averageFresnel = spectralScalarFromLinearSrgb(averageFresnel, lambdaNm).xxx;
    BSDFEval eval;
    eval.value = F * ggxEnergyScale(params.energy, params.averageEnergy, averageFresnel) *
                 (Dm * G / (4.0 * cosTheta(wo) * cosTheta(wi)));
    eval.pdf = ggxReflectionPdf(wo, m, params);
    return eval;
}

BSDFEval evalGGX(BSDFMaterial material, float3 wo, float3 wi, GGXParams params, float lambdaNm, uint spectralMode) {
    BSDFEval eval;
    eval.value = float3(0.0);
    eval.pdf = 0.0;
    if (cosTheta(wo) <= 0.0 || cosTheta(wi) <= 0.0) {
        return eval;
    }

    float3 m;
    float woDotM;
    if (!ggxHalfVector(wo, wi, m, woDotM)) {
        return eval;
    }

    return evalGGXFromHalfVector(material, wo, wi, params, m, woDotM, lambdaNm, spectralMode);
}

float4 evalSpectralGGX(
    BSDFMaterial material,
    float3 wo,
    float3 wi,
    GGXParams params,
    float4 wavelengthsNm,
    out float pdf
) {
    pdf = 0.0;
    if (cosTheta(wo) <= 0.0 || cosTheta(wi) <= 0.0) {
        return float4(0.0);
    }

    float3 m;
    float woDotM;
    if (!ggxHalfVector(wo, wi, m, woDotM)) {
        return float4(0.0);
    }

    float Dm = ggxDistribution(m, params.alpha);
    float G = ggxMasking(wo, wi, params.alpha);
    pdf = ggxReflectionPdf(wo, m, params);
    float4 averageFresnel = spectralScalarFromLinearSrgb4(ggxConductorAverageFresnel(material), wavelengthsNm);
    return ggxConductorFresnel4(material, woDotM, wavelengthsNm) *
           ggxEnergyScale4(params.energy, params.averageEnergy, averageFresnel) *
           (Dm * G / (4.0 * cosTheta(wo) * cosTheta(wi)));
}

bool sampleGGXVNDF(float3 wo, GGXParams params, float2 u, out float3 m) {
    if (params.projectedWoLength <= GGX_EPSILON) {
        m = float3(0.0);
        return false;
    }

    float3 woStd = float3(wo.xy * params.alpha, wo.z) / params.projectedWoLength;
    float phi = 2.0 * VKRT_PI * u.x;
    float z = (1.0 - u.y) * (1.0 + woStd.z) - woStd.z;
    float sinTheta = sqrt(saturate(1.0 - z * z));
    float3 wiStd = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);

    float3 mStd = woStd + wiStd;
    float3 unstretchedM = float3(mStd.xy * params.alpha, mStd.z);
    float unstretchedM2 = dot(unstretchedM, unstretchedM);
    if (unstretchedM2 <= 0.0) {
        m = float3(0.0);
        return false;
    }

    m = unstretchedM * rsqrt(unstretchedM2);
    return m.z > 0.0;
}

bool sampleGGX(float3 wo, GGXParams params, inout uint rng, out float3 wi) {
    float2 u = float2(rand(rng), rand(rng));
    float3 m;
    if (!sampleGGXVNDF(wo, params, u, m)) {
        wi = float3(0.0);
        return false;
    }

    float woDotM = dot(wo, m);
    if (woDotM <= 0.0) {
        wi = float3(0.0);
        return false;
    }

    wi = 2.0f * woDotM * m - wo;
    return cosTheta(wi) > 0.0;
}

#endif
