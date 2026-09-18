#ifndef VKRT_BSDF_SAMPLE_SPECTRAL_SLANG
#define VKRT_BSDF_SAMPLE_SPECTRAL_SLANG

#include "./principled.slang"

SpectralBSDFSample sampleSpectralBSDF(BSDFState state, ShadingBasis basis, float4 wavelengthsNm, inout uint rng) {
    SpectralBSDFSample sample = {};
    if (cosTheta(state.wo) <= 0.0) return sample;

    BSDFDirectionSample directionSample;
    if (!sampleBSDFDirection(state, rng, directionSample)) return sample;
    if (!bsdfDirectionIsValid(state, directionSample.wi)) return sample;

    sample.isTransmission = directionSample.isTransmission;
    sample.wi = localToWorld(directionSample.wi, basis);

    if (sample.isTransmission != 0u && dielectricIsIdentity(state.material, state.wavelengthNm, state.spectralMode)) {
        float4 color = spectralScalarFromLinearSrgb4(saturate(bsdfTransmissionColor(state.material)), wavelengthsNm);
        float attenuation = useInteriorDielectricInterface(state)
                              ? 1.0
                              : materialTransmissionStackAttenuation(state.material, state.wo);
        sample.techniquePdf = state.sampleWeights.dielectric.xxxx;
        sample.weight = color * ((1.0 - state.material.metallic) * state.material.transmission * attenuation /
                                 sample.techniquePdf.x);
        [unroll] for (uint lane = 0u; lane < 4u; lane++) {
            if (!dielectricIsIdentity(state.material, wavelengthsNm[lane], 1u)) {
                sample.techniquePdf[lane] = 0.0;
                sample.weight[lane] = 0.0;
            }
        }
        return sample;
    }

    float4 techniquePdf = float4(0.0);
    float4 spectralValue = evalSpectralBSDF(state, directionSample.wi, wavelengthsNm, techniquePdf);
    float sampledPdf = techniquePdf.x;
    if (!(sampledPdf > 0.0)) return sample;

    sample.weight = spectralValue * absCosTheta(directionSample.wi) / sampledPdf;
    sample.techniquePdf = techniquePdf;
    return sample;
}

#endif
