#ifndef VKRT_BSDF_SAMPLE_SPECTRAL_SLANG
#define VKRT_BSDF_SAMPLE_SPECTRAL_SLANG

#include "./principled.slang"

SpectralBSDFSample sampleSpectralBSDF(BSDFState state, ShadingBasis basis, float4 wavelengthsNm, inout uint rng) {
    SpectralBSDFSample sample = {};
    if (cosTheta(state.wo) <= 0.0) return sample;

    BSDFDirectionSample directionSample;
    if (!sampleBSDFDirection(state, rng, directionSample)) return sample;

    sample.isTransmission = directionSample.isTransmission;
    sample.wi = localToWorld(directionSample.wi, basis);

    float4 techniquePdf = float4(0.0);
    float4 spectralValue = evalSpectralBSDF(state, directionSample.wi, wavelengthsNm, techniquePdf);
    float sampledPdf = techniquePdf.x;
    if (!(sampledPdf > 0.0)) return sample;

    sample.weight = spectralValue * absCosTheta(directionSample.wi) / sampledPdf;
    sample.techniquePdf = techniquePdf;
    return sample;
}

#endif
