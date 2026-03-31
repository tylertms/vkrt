#ifndef VKRT_BSDF_BASE_MEDIUM_SLANG
#define VKRT_BSDF_BASE_MEDIUM_SLANG

static const uint VKRT_MEDIUM_FLAG_REFRACTIVE_ACTIVE = 1u << 0;
static const uint VKRT_MEDIUM_FLAG_ABSORPTION_ACTIVE = 1u << 1;

struct MediumState {
    uint flags = 0u;
    float3 absorptionSigma;
    float4 spectralAbsorptionSigma;

    __init() {
        flags = 0u;
        absorptionSigma = float3(0.0);
        spectralAbsorptionSigma = float4(0.0);
    }

    bool refractiveActive() {
        return (flags & VKRT_MEDIUM_FLAG_REFRACTIVE_ACTIVE) != 0u;
    }

    bool absorptionActive() {
        return (flags & VKRT_MEDIUM_FLAG_ABSORPTION_ACTIVE) != 0u;
    }

    [mutating] void setRefractiveActive(bool enabled) {
        flags = enabled ? (flags | VKRT_MEDIUM_FLAG_REFRACTIVE_ACTIVE) : (flags & ~VKRT_MEDIUM_FLAG_REFRACTIVE_ACTIVE);
    }

        [mutating] void setAbsorptionActive(bool enabled) {
        flags = enabled ? (flags | VKRT_MEDIUM_FLAG_ABSORPTION_ACTIVE) : (flags & ~VKRT_MEDIUM_FLAG_ABSORPTION_ACTIVE);
    }
};

bool materialHasAbsorption(BSDFMaterial material) {
    return material.absorptionCoefficient > 0.0 && any(material.attenuationColor < float3(0.9999));
}

bool materialMediumIsRefractive(BSDFMaterial material) {
    return material.transmission > 0.0 && interfaceIor(material) > 1.0 + 1e-4;
}

float3 materialAbsorptionSigma(BSDFMaterial material) {
    float3 absorptionTint = max(saturate(material.attenuationColor), float3(1e-6));
    return -log(absorptionTint) * material.absorptionCoefficient;
}

float3 materialAbsorptionSigma(BSDFMaterial material, float lambdaNm, uint spectralMode) {
    if (spectralMode == 0u) {
        return materialAbsorptionSigma(material);
    }

    float absorptionTint = max(spectralScalarFromLinearSrgb(saturate(material.attenuationColor), lambdaNm), 1e-6);
    return (-log(absorptionTint) * material.absorptionCoefficient).xxx;
}

float4 materialAbsorptionSigma4(BSDFMaterial material, float4 wavelengthsNm) {
    float4 absorptionTint =
        max(spectralScalarFromLinearSrgb4(saturate(material.attenuationColor), wavelengthsNm), float4(1e-6));
    return -log(absorptionTint) * material.absorptionCoefficient;
}

bool mediumHasActiveBoundary(MediumState medium) {
    return medium.flags != 0u;
}

float3 mediumTransmittance(MediumState medium, float distance) {
    return medium.absorptionActive() ? exp(-medium.absorptionSigma * distance) : float3(1.0);
}

float4 mediumSpectralTransmittance(MediumState medium, float distance) {
    return medium.absorptionActive() ? exp(-medium.spectralAbsorptionSigma * distance) : float4(1.0);
}

void updateMediumStateFromTransmission(
    BSDFMaterial material,
    uint frontFace,
    uint isTransmission,
    float lambdaNm,
    uint spectralMode,
    inout MediumState medium
) {
    if (isTransmission == 0u) return;

    bool enteringMedium = frontFace != 0u;
    medium.setRefractiveActive(enteringMedium && materialMediumIsRefractive(material));
    medium.setAbsorptionActive(enteringMedium && materialHasAbsorption(material));
    medium.absorptionSigma =
        medium.absorptionActive() ? materialAbsorptionSigma(material, lambdaNm, spectralMode) : float3(0.0);
    medium.spectralAbsorptionSigma = medium.absorptionSigma.xxxx;
}

void updateMediumStateFromTransmissionSpectral(
    BSDFMaterial material,
    uint frontFace,
    uint isTransmission,
    float4 wavelengthsNm,
    inout MediumState medium
) {
    if (isTransmission == 0u) return;

    bool enteringMedium = frontFace != 0u;
    medium.setRefractiveActive(enteringMedium && materialMediumIsRefractive(material));
    medium.setAbsorptionActive(enteringMedium && materialHasAbsorption(material));
    if (!medium.absorptionActive()) {
        medium.absorptionSigma = float3(0.0);
        medium.spectralAbsorptionSigma = float4(0.0);
        return;
    }

    medium.spectralAbsorptionSigma = materialAbsorptionSigma4(material, wavelengthsNm);
    medium.absorptionSigma = medium.spectralAbsorptionSigma.xxx;
}

#endif
