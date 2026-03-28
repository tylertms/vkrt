#ifndef VKRT_UTILITY_WAVELENGTH_SLANG
#define VKRT_UTILITY_WAVELENGTH_SLANG

#include "random.slang"

static const float VKRT_WAVELENGTH_MIN_NM = 360.0;
static const float VKRT_WAVELENGTH_MAX_NM = 830.0;
static const float VKRT_WAVELENGTH_RANGE_NM = VKRT_WAVELENGTH_MAX_NM - VKRT_WAVELENGTH_MIN_NM;

struct WavelengthSample {
    float lambdaNm;
    float invPdf;
};

float wavelengthFromUnitSample(float u) {
    return VKRT_WAVELENGTH_MIN_NM + saturate(u) * VKRT_WAVELENGTH_RANGE_NM;
}

float wavelengthPdfUniform() {
    return 1.0 / VKRT_WAVELENGTH_RANGE_NM;
}

WavelengthSample sampleUniformWavelength(inout uint rng) {
    WavelengthSample sample;
    sample.lambdaNm = wavelengthFromUnitSample(rand(rng));
    sample.invPdf = 1.0 / wavelengthPdfUniform();
    return sample;
}

#endif
