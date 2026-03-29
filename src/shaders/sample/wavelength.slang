#ifndef VKRT_UTILITY_WAVELENGTH_SLANG
#define VKRT_UTILITY_WAVELENGTH_SLANG

#include "random.slang"

static const float VKRT_WAVELENGTH_MIN_NM = 360.0;
static const float VKRT_WAVELENGTH_MAX_NM = 830.0;
static const float VKRT_WAVELENGTH_RANGE_NM = VKRT_WAVELENGTH_MAX_NM - VKRT_WAVELENGTH_MIN_NM;
static const float VKRT_INV_UINT32 = 1.0 / 4294967296.0;
static const uint VKRT_HERO_WAVELENGTH_LANE_COUNT = 4u;
static const float VKRT_HERO_WAVELENGTH_INV_LANE_COUNT = 0.25;
static const float4 VKRT_HERO_WAVELENGTH_OFFSETS = float4(0.0, 0.25, 0.5, 0.75);

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

float sampleUniformWavelengthUnit(inout uint rng, uint sampleIndex) {
    float radInvB2 = float(reverseBits32(sampleIndex)) * VKRT_INV_UINT32;
    return frac(radInvB2 + rand(rng));
}

WavelengthSample sampleUniformWavelength(inout uint rng, uint sampleIndex) {
    WavelengthSample sample;
    float unitSample = sampleUniformWavelengthUnit(rng, sampleIndex);

    sample.lambdaNm = VKRT_WAVELENGTH_MIN_NM + saturate(unitSample) * VKRT_WAVELENGTH_RANGE_NM;
    sample.invPdf = VKRT_WAVELENGTH_RANGE_NM;

    return sample;
}

float4 sampleHeroWavelengths4(inout uint rng, uint sampleIndex) {
    float unitSample = sampleUniformWavelengthUnit(rng, sampleIndex);
    return VKRT_WAVELENGTH_MIN_NM + frac(unitSample + VKRT_HERO_WAVELENGTH_OFFSETS) * VKRT_WAVELENGTH_RANGE_NM;
}

float4 heroWavelengthInvPdf4() {
    return (VKRT_WAVELENGTH_RANGE_NM * VKRT_HERO_WAVELENGTH_INV_LANE_COUNT).xxxx;
}

#endif
