#ifndef VKRT_BSDF_GGX_SLANG
#define VKRT_BSDF_GGX_SLANG

#include "../common.slang"

// GGX specular reflection lobe:
// Microfacet glossy lobe for metals and dielectrics.
// Roughness widens the highlight, anisotropy stretches it, and Fresnel controls edge brightening.
//
// References:
// Anisotropic roughness: https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf
// Bounded VNDF k: https://gpuopen.com/download/Bounded_VNDF_Sampling_for_Smith-GGX_Reflections.pdf
// Bounded VNDF sampling: https://gpuopen.com/download/Bounded_VNDF_Sampling_for_Smith-GGX_Reflections_Supplementary_Document.pdf
// GGX masking/shadowing: https://jcgt.org/published/0007/04/01/paper.pdf
// Microfacet reflection basics: https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory
// Disney metallic Fresnel: https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf
// Fresnel building blocks: https://www.pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission

static const float GGX_MIN_ALPHA = 1e-3;
static const float GGX_EPSILON = 1e-6;

struct GGXParams {
    float2 alpha;
    float vndfK;
    float projectedWoLength;
};

float2 ggxAlpha(Material material) {
    float roughness = max(material.roughness, GGX_MIN_ALPHA);
    float aspect = sqrt(max(1.0 - 0.9 * material.anisotropic, GGX_MIN_ALPHA));
    float a = roughness * roughness;
    return max(float2(a / aspect, a * aspect), float2(GGX_MIN_ALPHA));
}

float ggxVndfK(float3 wo, float2 alpha) {
    float a = saturate(min(alpha.x, alpha.y));
    float s = 1.0 + length(wo.xy);
    float a2 = a * a;
    float s2 = s * s;
    return (1.0 - a2) * s2 / max(s2 + a2 * wo.z * wo.z, GGX_EPSILON);
}

float ggxLambda(float3 w, float2 alpha) {
    float2 alpha2 = alpha * alpha;
    float z2 = max(w.z * w.z, GGX_EPSILON);
    float slope2 = dot(alpha2 * w.xy, w.xy) / z2;
    return 0.5 * (sqrt(1.0 + slope2) - 1.0);
}

float ggxDistribution(float3 m, float2 alpha) {
    if (m.z <= 0.0) return 0.0;
    float c = (m.x * m.x) / (alpha.x * alpha.x) +
        (m.y * m.y) / (alpha.y * alpha.y) + m.z * m.z;
    return 1.0 / (VKRT_PI * alpha.x * alpha.y * c * c);
}

float ggxMasking(float3 wo, float3 wi, float2 alpha) {
    return 1.0 / (1.0 + ggxLambda(wo, alpha) + ggxLambda(wi, alpha));
}

float ggxProjectedLength(float3 wo, float2 alpha) {
    float2 stretchedXY = alpha * wo.xy;
    return sqrt(dot(stretchedXY, stretchedXY) + wo.z * wo.z);
}

GGXParams makeGGXParams(Material material, float3 wo) {
    GGXParams params;
    params.alpha = ggxAlpha(material);
    params.vndfK = ggxVndfK(wo, params.alpha);
    params.projectedWoLength = ggxProjectedLength(wo, params.alpha);
    return params;
}

bool ggxHalfVector(float3 wo, float3 wi, out float3 m, out float woDotM) {
    float3 h = wo + wi;
    float h2 = dot(h, h);
    if (h2 <= GGX_EPSILON) {
        m = float3(0.0);
        woDotM = 0.0;
        return false;
    }

    m = h * rsqrt(h2);
    woDotM = dot(wo, m);
    return woDotM > 0.0;
}

float3 ggxFresnel(Material material, float cosTheta) {
    float3 dielectric = fresnelSchlick(cosTheta, bsdfDielectricSpecularF0(material));
    float3 conductor = materialHasConductor(material)
        ? fresnelConductor(cosTheta, max(material.eta, float3(1e-3)), max(material.k, float3(0.0)))
        : fresnelSchlick(cosTheta, material.baseColor);
    return lerp(dielectric, conductor, material.metallic);
}

BSDFEval evalGGXFromHalfVector(Material material, float3 wo, float3 wi, GGXParams params, float3 m, float woDotM) {
    float Dm = ggxDistribution(m, params.alpha);
    float G = ggxMasking(wo, wi, params.alpha);
    float3 F = ggxFresnel(material, woDotM);
    BSDFEval eval;
    eval.value = F * (Dm * G / max(4.0 * wo.z * wi.z, GGX_EPSILON));
    eval.pdf = Dm / max(2.0 * (params.vndfK * wo.z + params.projectedWoLength), GGX_EPSILON);
    return eval;
}

BSDFEval evalGGX(Material material, float3 wo, float3 wi, GGXParams params) {
    BSDFEval eval;
    eval.value = float3(0.0);
    eval.pdf = 0.0;
    if (wo.z <= 0.0 || wi.z <= 0.0) {
        return eval;
    }

    float3 m;
    float woDotM;
    if (!ggxHalfVector(wo, wi, m, woDotM)) {
        return eval;
    }

    return evalGGXFromHalfVector(material, wo, wi, params, m, woDotM);
}

bool sampleGGXVNDF(float3 wo, GGXParams params, float2 u, out float3 m) {
    if (params.projectedWoLength <= GGX_EPSILON) {
        m = float3(0.0);
        return false;
    }

    float3 woStd = float3(wo.xy * params.alpha, wo.z) / params.projectedWoLength;
    float phi = 2.0 * VKRT_PI * u.x;
    float b = params.vndfK * woStd.z;
    float z = (1.0 - u.y) * (1.0 + b) - b;
    float sinTheta = sqrt(saturate(1.0 - z * z));
    float3 wiStd = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);

    float3 mStd = woStd + wiStd;
    float mStd2 = dot(mStd, mStd);
    if (mStd2 <= GGX_EPSILON) {
        m = float3(0.0);
        return false;
    }

    float3 unstretchedM = float3(mStd.xy * params.alpha, mStd.z);
    float unstretchedM2 = dot(unstretchedM, unstretchedM);
    if (unstretchedM2 <= GGX_EPSILON) {
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
    return wi.z > 0.0;
}

#endif
