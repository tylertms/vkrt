#ifndef VKRT_BSDF_BSDF_SLANG
#define VKRT_BSDF_BSDF_SLANG

#include "principled.slang"

BSDFSample sampleBSDF(BSDFState state, ShadingBasis basis, inout uint rng) {
    BSDFSample sample = {};
    if (cosTheta(state.wo) <= 0.0) return sample;

    BSDFDirectionSample directionSample;
    if (!sampleBSDFDirection(state, rng, directionSample)) return sample;

    sample.isTransmission = directionSample.isTransmission;
    sample.wi = localToWorld(directionSample.wi, basis);

    BSDFEval eval = state.spectralMode != 0u ? evalSingleWavelengthBSDF(state, directionSample.wi)
                                             : evalBSDF(state, directionSample.wi);
    if (eval.pdf <= 0.0) {
        return sample;
    }

    sample.pdf = eval.pdf;
    sample.weight = eval.value * absCosTheta(directionSample.wi) / sample.pdf;

    return sample;
}

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
