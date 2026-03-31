#ifndef VKRT_BSDF_BASE_FRESNEL_SLANG
#define VKRT_BSDF_BASE_FRESNEL_SLANG

float dielectricF0(float eta) {
    float numerator = eta - 1.0;
    float denominator = eta + 1.0;
    float f0 = numerator / max(denominator, 1e-6);
    return f0 * f0;
}

bool materialHasConductor(BSDFMaterial material) {
    return any(material.k > 0.0);
}

float3 bsdfDielectricSpecularF0(BSDFMaterial material) {
    float dielectric = dielectricF0(material.ior);
    float specularScale = material.specular / 0.5;
    float3 tint = lerp(float3(1.0), bsdfTintColor(material.baseColor), saturate(material.specularTint));
    return saturate(dielectric * specularScale * tint);
}

float bsdfDielectricSpecularF0Luminance(BSDFMaterial material) {
    return saturate(linearSrgbLuminance(bsdfDielectricSpecularF0(material)));
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

    float sinTheta2I = max(1.0 - cosThetaI * cosThetaI, 0.0);
    float sinTheta2T = sinTheta2I / max(eta * eta, 1e-6);
    if (sinTheta2T >= 1.0) {
        return 1.0;
    }

    float cosThetaT = sqrt(max(1.0 - sinTheta2T, 0.0));
    float rParallel = (eta * cosThetaI - cosThetaT) / max(eta * cosThetaI + cosThetaT, 1e-6);
    float rPerpendicular = (cosThetaI - eta * cosThetaT) / max(cosThetaI + eta * cosThetaT, 1e-6);
    return 0.5 * (rParallel * rParallel + rPerpendicular * rPerpendicular);
}

float3 fresnelConductor(float cosTheta, float3 eta, float3 k) {
    float cosThetaI = saturate(cosTheta);
    float cosTheta2I = cosThetaI * cosThetaI;
    float sinTheta2I = max(1.0 - cosTheta2I, 0.0);
    float3 eta2 = eta * eta;
    float3 k2 = k * k;

    float3 t0 = eta2 - k2 - sinTheta2I;
    float3 a2PlusB2 = sqrt(max(t0 * t0 + 4.0 * eta2 * k2, float3(0.0)));
    float3 t1 = a2PlusB2 + cosTheta2I;
    float3 a = sqrt(max(0.5 * (a2PlusB2 + t0), float3(0.0)));
    float3 t2 = 2.0 * cosThetaI * a;
    float3 rs = (t1 - t2) / max(t1 + t2, float3(1e-6));

    float3 t3 = cosTheta2I * a2PlusB2 + sinTheta2I * sinTheta2I;
    float3 t4 = t2 * sinTheta2I;
    float3 rp = rs * ((t3 - t4) / max(t3 + t4, float3(1e-6)));
    return 0.5 * (rp + rs);
}

#endif
