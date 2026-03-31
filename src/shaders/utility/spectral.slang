#ifndef VKRT_UTILITY_SPECTRAL_SLANG
#define VKRT_UTILITY_SPECTRAL_SLANG

#include "../sampling/wavelength.slang"
#include "../scene/resources.slang"
#include "./rgb2spec.slang"

// References:
// CIE XYZ fits: Wyman, Sloan, Shirley, 2013 - https://jcgt.org/published/0002/02/01/
// Hero wavelengths: Wilkie et al., 2014 - https://cgg.mff.cuni.cz/publications/hero-wavelength-spectral-sampling/
// Fixed-size sampled wavelengths: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Radiometry%2C_Spectra%2C_and_Color/Representing_Spectral_Distributions
// Fraunhofer line wavelengths / Abbe number relation: https://en.wikipedia.org/wiki/Abbe_number

static const float3x3 VKRT_XYZ_TO_LINEAR_SRGB =
    float3x3(3.2404542, -1.5371385, -0.4985314, -0.9692660, 1.8760108, 0.0415560, 0.0556434, -0.2040259, 1.0572252);
static const float VKRT_CIE_Y_INTEGRAL_1931_FIT = 106.9461715;

static const float VKRT_FRAUNHOFER_C_UM = 0.6562725;
static const float VKRT_FRAUNHOFER_D_UM = 0.5875618;
static const float VKRT_FRAUNHOFER_F_UM = 0.4861327;

bool spectralRenderingEnabled() {
    return VKRT_SCENE_RENDER_MODE_FROM_RENDER_SETTINGS(scene.packedRenderSettings) == VKRT_RENDER_MODE_SPECTRAL;
}

bool heroWavelengthSamplingEnabled() {
    return spectralRenderingEnabled() &&
           VKRT_SCENE_SPECTRAL_SAMPLING_MODE_FROM_RENDER_SETTINGS(scene.packedRenderSettings) ==
               VKRT_SPECTRAL_SAMPLING_MODE_HERO;
}

float spectralScalarFromLinearSrgb(float3 rgb, float lambdaNm) {
    float maxValue = max(rgb.x, max(rgb.y, rgb.z));
    if (maxValue <= 0.0) {
        return 0.0;
    }

    if (maxValue <= 1.0) {
        return rgb2specEval(scene.rgb2specSRGB, rgb2specSRGBTable, rgb, lambdaNm);
    }

    return maxValue * rgb2specEval(scene.rgb2specSRGB, rgb2specSRGBTable, rgb / maxValue, lambdaNm);
}

float4 spectralScalarFromLinearSrgb4(float3 rgb, float4 lambdaNm) {
    float maxValue = max(rgb.x, max(rgb.y, rgb.z));
    if (maxValue <= 0.0) {
        return float4(0.0);
    }

    float scale = maxValue <= 1.0 ? 1.0 : maxValue;
    float3 coeff = rgb2specFetch(scene.rgb2specSRGB, rgb2specSRGBTable, rgb / scale);
    return scale * float4(
                       rgb2specEvalCoeffs(coeff, lambdaNm.x),
                       rgb2specEvalCoeffs(coeff, lambdaNm.y),
                       rgb2specEvalCoeffs(coeff, lambdaNm.z),
                       rgb2specEvalCoeffs(coeff, lambdaNm.w)
                   );
}

float spectralXFit1931(float lambdaNm) {
    float t1 = (lambdaNm - 442.0) * (lambdaNm < 442.0 ? 0.0624 : 0.0374);
    float t2 = (lambdaNm - 599.8) * (lambdaNm < 599.8 ? 0.0264 : 0.0323);
    float t3 = (lambdaNm - 501.1) * (lambdaNm < 501.1 ? 0.0490 : 0.0382);
    return 0.362 * exp(-0.5 * t1 * t1) + 1.056 * exp(-0.5 * t2 * t2) - 0.065 * exp(-0.5 * t3 * t3);
}

float spectralYFit1931(float lambdaNm) {
    float t1 = (lambdaNm - 568.8) * (lambdaNm < 568.8 ? 0.0213 : 0.0247);
    float t2 = (lambdaNm - 530.9) * (lambdaNm < 530.9 ? 0.0613 : 0.0322);
    return 0.821 * exp(-0.5 * t1 * t1) + 0.286 * exp(-0.5 * t2 * t2);
}

float spectralZFit1931(float lambdaNm) {
    float t1 = (lambdaNm - 437.0) * (lambdaNm < 437.0 ? 0.0845 : 0.0278);
    float t2 = (lambdaNm - 459.0) * (lambdaNm < 459.0 ? 0.0385 : 0.0725);
    return 1.217 * exp(-0.5 * t1 * t1) + 0.681 * exp(-0.5 * t2 * t2);
}

float3 spectralXYZ1931(float lambdaNm) {
    return max(float3(spectralXFit1931(lambdaNm), spectralYFit1931(lambdaNm), spectralZFit1931(lambdaNm)), float3(0.0));
}

float3 xyzToLinearSrgb(float3 xyz) {
    return mul(VKRT_XYZ_TO_LINEAR_SRGB, xyz);
}

float3 spectralSampleToXYZ(float scalarValue, WavelengthSample wavelength) {
    float xyzScale = (scalarValue * wavelength.invPdf) / VKRT_CIE_Y_INTEGRAL_1931_FIT;
    return spectralXYZ1931(wavelength.lambdaNm) * xyzScale;
}

float3 spectralSample4ToXYZ(float4 scalarValue, float4 lambdaNm, float4 invPdf) {
    float3 xyz = float3(0.0);

    xyz += spectralXYZ1931(lambdaNm.x) * ((scalarValue.x * invPdf.x) / VKRT_CIE_Y_INTEGRAL_1931_FIT);
    xyz += spectralXYZ1931(lambdaNm.y) * ((scalarValue.y * invPdf.y) / VKRT_CIE_Y_INTEGRAL_1931_FIT);
    xyz += spectralXYZ1931(lambdaNm.z) * ((scalarValue.z * invPdf.z) / VKRT_CIE_Y_INTEGRAL_1931_FIT);
    xyz += spectralXYZ1931(lambdaNm.w) * ((scalarValue.w * invPdf.w) / VKRT_CIE_Y_INTEGRAL_1931_FIT);

    return xyz;
}

float heroWavelengthBalanceWeight(float4 techniquePathPdf) {
    float combinedPdf = dot(techniquePathPdf, float4(1.0));
    return combinedPdf > 0.0 ? techniquePathPdf.x / combinedPdf : 0.0;
}

float maxComponent4(float4 value) {
    return max(max(value.x, value.y), max(value.z, value.w));
}

float dispersiveIor(float ior, float abbeNumber, float lambdaNm) {
    if (ior <= 1.0 + 1e-4 || abbeNumber <= 0.0) {
        return max(ior, 1.0);
    }

    float lambdaUm = max(lambdaNm * 0.001, 1e-4);
    float nFMinusNC = (ior - 1.0) / max(abbeNumber, 1e-6);
    float invF2 = 1.0 / (VKRT_FRAUNHOFER_F_UM * VKRT_FRAUNHOFER_F_UM);
    float invC2 = 1.0 / (VKRT_FRAUNHOFER_C_UM * VKRT_FRAUNHOFER_C_UM);
    float B = nFMinusNC / (invF2 - invC2);
    float A = ior - B / (VKRT_FRAUNHOFER_D_UM * VKRT_FRAUNHOFER_D_UM);
    return max(A + B / (lambdaUm * lambdaUm), 1.0);
}

#endif
