#ifndef VKRT_BSDF_GGX_SLANG
#define VKRT_BSDF_GGX_SLANG

#include "../common.slang"

static const float GGX_MIN_ALPHA = 1e-3;
static const float GGX_EPSILON = 1e-6;

// Disney anisotropic roughness remapping.
// https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf
float2 ggxAlpha(Material material) {
    float roughness = max(material.roughness, GGX_MIN_ALPHA);
    float aspect = sqrt(max(1.0 - 0.9 * material.anisotropic, GGX_MIN_ALPHA));
    float a = roughness * roughness;
    return max(float2(a / aspect, a * aspect), float2(GGX_MIN_ALPHA));
}

// Bounded spherical-cap constant k from Eto and Tokuyoshi 2023, Eq. (5)-(6).
// https://gpuopen.com/download/Bounded_VNDF_Sampling_for_Smith-GGX_Reflections.pdf
// https://gpuopen.com/download/Bounded_VNDF_Sampling_for_Smith-GGX_Reflections_Supplementary_Document.pdf
float ggxBoundedVNDFK(float3 wo, float2 alpha) {
    float a = saturate(min(alpha.x, alpha.y));
    float s = 1.0 + length(wo.xy);
    float a2 = a * a;
    float s2 = s * s;
    return (1.0 - a2) * s2 / max(s2 + a2 * wo.z * wo.z, GGX_EPSILON);
}

// GGX NDF and Smith masking-shadowing terms.
// https://jcgt.org/published/0007/04/01/paper.pdf
// https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory
float ggxLambda(float3 w, float2 alpha) {
    float2 alpha2 = alpha * alpha;
    float z2 = max(w.z * w.z, GGX_EPSILON);
    float slope2 = dot(alpha2 * w.xy, w.xy) / z2;
    return 0.5 * (sqrt(1.0 + slope2) - 1.0);
}

float ggxD(float3 m, float2 alpha) {
    if (m.z <= 0.0) return 0.0;
    float c = (m.x * m.x) / (alpha.x * alpha.x) +
        (m.y * m.y) / (alpha.y * alpha.y) + m.z * m.z;
    return 1.0 / (VKRT_PI * alpha.x * alpha.y * c * c);
}

float ggxG2(float3 wo, float3 wi, float2 alpha) {
    return 1.0 / (1.0 + ggxLambda(wo, alpha) + ggxLambda(wi, alpha));
}

float ggxProjectedLength(float3 wo, float2 alpha) {
    float2 stretchedXY = alpha * wo.xy;
    return sqrt(dot(stretchedXY, stretchedXY) + wo.z * wo.z);
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

// Disney-style metallic workflow on top of PBRT Fresnel building blocks.
// https://www.pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission
// https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf
float3 ggxFresnel(BSDFInput input, float cosTheta) {
    float dielectric = fresnelDielectric(cosTheta, input.material.ior);
    float3 conductor = materialHasConductor(input.material)
        ? fresnelConductor(cosTheta, max(input.material.eta, float3(1e-3)), max(input.material.k, float3(0.0)))
        : fresnelSchlick(cosTheta, input.material.baseColor);
    return lerp(dielectric.xxx, conductor, input.material.metallic);
}

void ggxEvalPdfFromSample(
    BSDFInput input, float3 wo, float3 wi, float2 alpha,
    float3 m, float woDotM, float k, float projectedLength,
    out float3 value, out float pdf
) {
    float Dm = ggxD(m, alpha);
    float G = ggxG2(wo, wi, alpha);
    float3 F = ggxFresnel(input, woDotM);
    value = F * (Dm * G / max(4.0 * wo.z * wi.z, GGX_EPSILON));
    pdf = Dm / max(2.0 * (k * wo.z + projectedLength), GGX_EPSILON);
}

bool ggxEvalPdfLocal(
    BSDFInput input, float3 wo, float3 wi, float2 alpha,
    float k, float projectedLength,
    out float3 value, out float pdf
) {
    value = float3(0.0);
    pdf = 0.0;
    if (wo.z <= 0.0 || wi.z <= 0.0) {
        return false;
    }

    float3 m;
    float woDotM;
    if (!ggxHalfVector(wo, wi, m, woDotM)) {
        return false;
    }

    ggxEvalPdfFromSample(input, wo, wi, alpha, m, woDotM, k, projectedLength, value, pdf);
    return true;
}

bool sampleGGXBoundedVNDF(
    float3 wo, float2 alpha, float k, float projectedLength,
    float2 u, out float3 m
) {
    if (projectedLength <= GGX_EPSILON) {
        m = float3(0.0);
        return false;
    }

    float3 woStd = float3(wo.xy * alpha, wo.z) / projectedLength;
    float phi = 2.0 * VKRT_PI * u.x;
    float b = k * woStd.z;
    float z = (1.0 - u.y) * (1.0 + b) - b;
    float sinTheta = sqrt(saturate(1.0 - z * z));
    float3 wiStd = float3(sinTheta * cos(phi), sinTheta * sin(phi), z);

    float3 mStd = woStd + wiStd;
    float mStd2 = dot(mStd, mStd);
    if (mStd2 <= GGX_EPSILON) {
        m = float3(0.0);
        return false;
    }

    float3 unstretchedM = float3(mStd.xy * alpha, mStd.z);
    float unstretchedM2 = dot(unstretchedM, unstretchedM);
    if (unstretchedM2 <= GGX_EPSILON) {
        m = float3(0.0);
        return false;
    }

    m = unstretchedM * rsqrt(unstretchedM2);
    return m.z > 0.0;
}

bool sampleGGXDirectionLocal(
    float3 wo, float2 alpha, float k, float projectedLength,
    inout uint rng, out float3 wi, out float3 m, out float woDotM
) {
    float2 u = float2(rand(rng), rand(rng));
    if (!sampleGGXBoundedVNDF(wo, alpha, k, projectedLength, u, m)) {
        wi = float3(0.0);
        woDotM = 0.0;
        return false;
    }

    woDotM = dot(wo, m);
    if (woDotM <= 0.0) {
        wi = float3(0.0);
        return false;
    }

    wi = 2.0f * woDotM * m - wo;
    return wi.z > 0.0;
}

#endif
