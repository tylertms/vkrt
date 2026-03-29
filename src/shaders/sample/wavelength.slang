#ifndef VKRT_UTILITY_WAVELENGTH_SLANG
#define VKRT_UTILITY_WAVELENGTH_SLANG

#include "random.slang"

static const float VKRT_WAVELENGTH_MIN_NM = 360.0;
static const float VKRT_WAVELENGTH_MAX_NM = 830.0;
static const float VKRT_WAVELENGTH_RANGE_NM = VKRT_WAVELENGTH_MAX_NM - VKRT_WAVELENGTH_MIN_NM;
static const float VKRT_INV_UINT32 = 1.0 / 4294967296.0;

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

uint reverseBits32(uint value) {
    value = ((value & 0x55555555u) << 1u) | ((value >> 1u) & 0x55555555u);
    value = ((value & 0x33333333u) << 2u) | ((value >> 2u) & 0x33333333u);
    value = ((value & 0x0f0f0f0fu) << 4u) | ((value >> 4u) & 0x0f0f0f0fu);
    value = ((value & 0x00ff00ffu) << 8u) | ((value >> 8u) & 0x00ff00ffu);
    return (value << 16u) | (value >> 16u);
}

float radicalInverseBase2(uint sampleIndex) {
    return float(reverseBits32(sampleIndex)) * VKRT_INV_UINT32;
}

float wavelengthUnitSample(uint sampleIndex, inout uint rng) {
    return frac(radicalInverseBase2(sampleIndex) + rand(rng));
}

WavelengthSample sampleUniformWavelength(inout uint rng, uint sampleIndex) {
    WavelengthSample sample;
    sample.lambdaNm = wavelengthFromUnitSample(wavelengthUnitSample(sampleIndex, rng));
    sample.invPdf = 1.0 / wavelengthPdfUniform();
    return sample;
}

WavelengthSample sampleUniformWavelength(inout uint rng) {
    return sampleUniformWavelength(rng, 0u);
}

#endif
