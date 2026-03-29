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

uint reverseBits32(uint value) {
    value = ((value & 0x55555555u) << 1u) | ((value >> 1u) & 0x55555555u);
    value = ((value & 0x33333333u) << 2u) | ((value >> 2u) & 0x33333333u);
    value = ((value & 0x0f0f0f0fu) << 4u) | ((value >> 4u) & 0x0f0f0f0fu);
    value = ((value & 0x00ff00ffu) << 8u) | ((value >> 8u) & 0x00ff00ffu);
    return (value << 16u) | (value >> 16u);
}

WavelengthSample sampleUniformWavelength(inout uint rng, uint sampleIndex) {
    WavelengthSample sample;

    float radInvB2 = float(reverseBits32(sampleIndex)) * VKRT_INV_UINT32;
    float unitSample = frac(radInvB2 + rand(rng));

    sample.lambdaNm = VKRT_WAVELENGTH_MIN_NM + saturate(unitSample) * VKRT_WAVELENGTH_RANGE_NM;
    sample.invPdf = VKRT_WAVELENGTH_RANGE_NM;

    return sample;
}

#endif
