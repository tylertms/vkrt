#ifndef VKRT_BSDF_COMMON_SLANG
#define VKRT_BSDF_COMMON_SLANG

#include "types.slang"
#include "../sample/random.slang"
#include "../camera/ray.slang"

// References:
// Basis construction: Duff et al., 2017 - https://graphics.pixar.com/library/OrthonormalB/paper.pdf
// Diffuse color workflow: Khronos Group, 2017 - https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
// Luminance weights: ITU-R BT.709, 2015 - https://www.itu.int/rec/R-REC-BT.709
// Disney BRDF: Burley, 2012 - https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
// Schlick Fresnel: Schlick, 1994 - https://diglib.eg.org/items/581037f7-1e5c-4859-a107-6dfeeb8f3ccb
// Dielectric/conductor Fresnel: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission

static const float VKRT_PI = 3.14159265358979323846;
static const float VKRT_INV_PI = 0.31830988618379067154;

struct ShadingBasis {
    float3 tangent;
    float3 bitangent;
    float3 normal;
};

float pow5(float value) {
    float value2 = value * value;
    return value2 * value2 * value;
}

float schlickWeight(float cosTheta) {
    return pow5(1.0 - saturate(cosTheta));
}

float cosTheta(float3 w) {
    return w.z;
}

float absCosTheta(float3 w) {
    return abs(w.z);
}

float cos2Theta(float3 w) {
    return w.z * w.z;
}

float sin2Theta(float3 w) {
    return saturate(1.0 - cos2Theta(w));
}

float sinTheta(float3 w) {
    return sqrt(sin2Theta(w));
}

float sinPhi(float3 w) {
    float s2t = sin2Theta(w);
    return s2t > 0.0 ? clamp(w.y * rsqrt(s2t), -1.0, 1.0) : 0.0;
}

float cosPhi(float3 w) {
    float s2t = sin2Theta(w);
    return s2t > 0.0 ? clamp(w.x * rsqrt(s2t), -1.0, 1.0) : 1.0;
}

float cosPhiDifference(float3 wi, float3 wo) {
    float s2tI = sin2Theta(wi);
    float s2tO = sin2Theta(wo);
    if (s2tI <= 0.0 || s2tO <= 0.0) return 0.0;
    float invSinProduct = rsqrt(s2tI * s2tO);
    return clamp((wi.x * wo.x + wi.y * wo.y) * invSinProduct, -1.0, 1.0);
}

float3 makeFallbackTangent(float3 normal) {
    float3 up = abs(normal.z) < VKRT_TANGENT_PARALLEL_THRESHOLD ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    return safeNormalize(cross(up, normal));
}

ShadingBasis makeShadingBasis(float3 normal, float4 tangentData) {
    ShadingBasis basis;
    basis.normal = safeNormalize(normal);

    float3 tangent = tangentData.xyz - basis.normal * dot(tangentData.xyz, basis.normal);
    if (dot(tangent, tangent) <= VKRT_SAFE_NORMALIZE_EPS) {
        tangent = makeFallbackTangent(basis.normal);
    } else {
        tangent = safeNormalize(tangent);
    }

    float handedness = tangentData.w < 0.0 ? -1.0 : 1.0;
    basis.tangent = tangent;
    basis.bitangent = cross(basis.normal, basis.tangent) * handedness;
    return basis;
}

float3 localToWorld(float3 localDir, ShadingBasis basis) {
    return basis.tangent * localDir.x + basis.bitangent * localDir.y + basis.normal * localDir.z;
}

float3 worldToLocal(float3 worldDir, ShadingBasis basis) {
    return float3(
        dot(worldDir, basis.tangent),
        dot(worldDir, basis.bitangent),
        dot(worldDir, basis.normal)
    );
}

float3 bsdfDiffuseColor(Material material) {
    return material.baseColor * (1.0 - material.metallic);
}

float bsdfLuminance(float3 value) {
    return dot(value, float3(0.2126, 0.7152, 0.0722));
}

float3 bsdfTintColor(float3 baseColor) {
    float luminance = bsdfLuminance(baseColor);
    if (luminance <= 0.0) {
        return float3(1.0);
    }

    return baseColor / luminance;
}

float sheenWeight(Material material) {
    return saturate(material.sheenTintWeight.w);
}

float3 sheenTint(Material material) {
    return saturate(material.sheenTintWeight.xyz);
}

float3 bsdfSheenColor(Material material) {
    return sheenTint(material) * sheenWeight(material);
}

float3 bsdfTransmissionColor(Material material) {
    if (material.transmission > 0.0 && material.metallic <= 0.0) {
        return float3(1.0);
    }

    return material.baseColor;
}

float interfaceIor(Material material) {
    return max(material.ior, 1.0);
}

float interfaceEta(Material material, uint frontFace) {
    float ior = interfaceIor(material);
    return frontFace != 0u ? ior : (1.0 / ior);
}

float interfaceRefractionEta(Material material, uint frontFace) {
    float ior = interfaceIor(material);
    return frontFace != 0u ? (1.0 / ior) : ior;
}

struct MediumState {
    bool refractiveActive;
    bool absorptionActive;
    float3 absorptionSigma;
};

bool materialHasAbsorption(Material material) {
    return material.absorptionCoefficient > 0.0 && any(material.attenuationColor < float3(0.9999));
}

bool materialMediumIsRefractive(Material material) {
    return material.transmission > 0.0 && interfaceIor(material) > 1.0 + 1e-4;
}

float3 materialAbsorptionSigma(Material material) {
    float3 absorptionTint = max(saturate(material.attenuationColor), float3(1e-6));
    return -log(absorptionTint) * material.absorptionCoefficient;
}

bool mediumHasActiveBoundary(MediumState medium) {
    return medium.refractiveActive || medium.absorptionActive;
}

float3 mediumTransmittance(MediumState medium, float distance) {
    return medium.absorptionActive ? exp(-medium.absorptionSigma * distance) : float3(1.0);
}

void updateMediumStateFromTransmission(
    Material material,
    uint frontFace,
    uint isTransmission,
    inout MediumState medium
) {
    if (isTransmission == 0u) return;

    bool enteringMedium = frontFace != 0u;
    medium.refractiveActive = enteringMedium && materialMediumIsRefractive(material);
    medium.absorptionActive = enteringMedium && materialHasAbsorption(material);
    medium.absorptionSigma = medium.absorptionActive ? materialAbsorptionSigma(material) : float3(0.0);
}

float dielectricF0(float eta) {
    float numerator = eta - 1.0;
    float denominator = eta + 1.0;
    float f0 = numerator / max(denominator, 1e-6);
    return f0 * f0;
}

bool materialHasConductor(Material material) {
    return any(material.k > 0.0);
}

float3 bsdfDielectricSpecularF0(Material material) {
    float dielectric = dielectricF0(material.ior);
    float specularScale = material.specular / 0.5;
    float3 tint = lerp(float3(1.0), bsdfTintColor(material.baseColor), saturate(material.specularTint));
    return saturate(dielectric * specularScale * tint);
}

float bsdfDielectricSpecularF0Luminance(Material material) {
    return saturate(bsdfLuminance(bsdfDielectricSpecularF0(material)));
}

float3 fresnelSchlick(float cosTheta, float3 f0) {
    float weight = schlickWeight(cosTheta);
    return f0 + (1.0 - f0) * weight;
}

float fresnelDielectric(float cosTheta, float eta) {
    float cosThetaI = clamp(cosTheta, -1.0, 1.0);
    if (cosThetaI < 0.0) {
        eta = 1.0 / max(eta, 1e-6);
        cosThetaI = -cosThetaI;
    }

    float sin2ThetaI = max(1.0 - cosThetaI * cosThetaI, 0.0);
    float sin2ThetaT = sin2ThetaI / max(eta * eta, 1e-6);
    if (sin2ThetaT >= 1.0) {
        return 1.0;
    }

    float cosThetaT = sqrt(max(1.0 - sin2ThetaT, 0.0));
    float rParallel = (eta * cosThetaI - cosThetaT) / max(eta * cosThetaI + cosThetaT, 1e-6);
    float rPerpendicular = (cosThetaI - eta * cosThetaT) / max(cosThetaI + eta * cosThetaT, 1e-6);
    return 0.5 * (rParallel * rParallel + rPerpendicular * rPerpendicular);
}

float3 fresnelConductor(float cosTheta, float3 eta, float3 k) {
    float cosThetaI = saturate(cosTheta);
    float cos2ThetaI = cosThetaI * cosThetaI;
    float sin2ThetaI = max(1.0 - cos2ThetaI, 0.0);
    float3 eta2 = eta * eta;
    float3 k2 = k * k;

    float3 t0 = eta2 - k2 - sin2ThetaI;
    float3 a2PlusB2 = sqrt(max(t0 * t0 + 4.0 * eta2 * k2, float3(0.0)));
    float3 t1 = a2PlusB2 + cos2ThetaI;
    float3 a = sqrt(max(0.5 * (a2PlusB2 + t0), float3(0.0)));
    float3 t2 = 2.0 * cosThetaI * a;
    float3 rs = (t1 - t2) / max(t1 + t2, float3(1e-6));

    float3 t3 = cos2ThetaI * a2PlusB2 + sin2ThetaI * sin2ThetaI;
    float3 t4 = t2 * sin2ThetaI;
    float3 rp = rs * ((t3 - t4) / max(t3 + t4, float3(1e-6)));
    return 0.5 * (rp + rs);
}

#endif
