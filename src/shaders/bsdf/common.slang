#ifndef VKRT_BSDF_COMMON_SLANG
#define VKRT_BSDF_COMMON_SLANG

#include "types.slang"
#include "../utility/random.slang"
#include "../utility/ray.slang"

// References:
// Basis construction: https://graphics.pixar.com/library/OrthonormalB/paper.pdf
// Diffuse color workflow: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
// Luminance weights: https://www.itu.int/rec/R-REC-BT.709
// Disney tint/F0 controls: https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf
// Schlick Fresnel: https://diglib.eg.org/items/581037f7-1e5c-4859-a107-6dfeeb8f3ccb
// Dielectric/conductor Fresnel: https://www.pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission

static const float VKRT_PI = 3.14159265358979323846;
static const float VKRT_INV_PI = 0.31830988618379067154;

void shadingBasis(float3 normal, out float3 tangent, out float3 bitangent) {
    float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    tangent = safeNormalize(cross(up, normal));
    bitangent = cross(normal, tangent);
}

float3 localToWorld(float3 localDir, float3 normal) {
    float3 tangent, bitangent;
    shadingBasis(normal, tangent, bitangent);
    return tangent * localDir.x + bitangent * localDir.y + normal * localDir.z;
}

float3 worldToLocal(float3 worldDir, float3 normal) {
    float3 tangent, bitangent;
    shadingBasis(normal, tangent, bitangent);
    return float3(
        dot(worldDir, tangent),
        dot(worldDir, bitangent),
        dot(worldDir, normal)
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

float dielectricF0(float eta) {
    float numerator = eta - 1.0;
    float denominator = eta + 1.0;
    float f0 = numerator / max(denominator, 1e-6);
    return f0 * f0;
}

float3 conductorF0(float3 eta, float3 k) {
    float3 etaMinusOne = eta - 1.0;
    float3 etaPlusOne = eta + 1.0;
    float3 numerator = etaMinusOne * etaMinusOne + k * k;
    float3 denominator = etaPlusOne * etaPlusOne + k * k;
    return numerator / max(denominator, float3(1e-6));
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

float3 bsdfSpecularF0(Material material) {
    float3 dielectric = bsdfDielectricSpecularF0(material);
    float3 conductor = materialHasConductor(material)
        ? conductorF0(max(material.eta, float3(1e-3)), max(material.k, float3(0.0)))
        : material.baseColor;
    return lerp(dielectric, conductor, material.metallic);
}

float3 fresnelSchlick(float cosTheta, float3 f0) {
    float m = 1.0 - saturate(cosTheta);
    float m2 = m * m;
    float m5 = m2 * m2 * m;
    return f0 + (1.0 - f0) * m5;
}

// eta is the relative IOR eta_t / eta_i for the current interface.
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

// eta and k are already relative to the incident medium.
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
