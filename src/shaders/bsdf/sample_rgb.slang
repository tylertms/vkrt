#ifndef VKRT_BSDF_SAMPLE_RGB_SLANG
#define VKRT_BSDF_SAMPLE_RGB_SLANG

#include "./principled.slang"

BSDFSample sampleBSDF(BSDFState state, ShadingBasis basis, inout uint rng) {
    BSDFSample sample = {};
    if (cosTheta(state.wo) <= 0.0) return sample;

    BSDFDirectionSample directionSample;
    if (!sampleBSDFDirection(state, rng, directionSample)) return sample;
    if (!bsdfDirectionIsValid(state, directionSample.wi)) return sample;

    sample.isTransmission = directionSample.isTransmission;
    sample.wi = localToWorld(directionSample.wi, basis);

    if (sample.isTransmission != 0u && dielectricIsIdentity(state.material, state.wavelengthNm, state.spectralMode)) {
        float3 color = bsdfTransmissionColor(state.material);
        if (state.spectralMode != 0u) {
            color = spectralScalarFromLinearSrgb(saturate(color), state.wavelengthNm).xxx;
        }
        float attenuation = useInteriorDielectricInterface(state)
                              ? 1.0
                              : materialTransmissionStackAttenuation(state.material, state.wo);
        sample.pdf = state.sampleWeights.dielectric;
        sample.weight =
            color * ((1.0 - state.material.metallic) * state.material.transmission * attenuation / sample.pdf);
        return sample;
    }

    BSDFEval eval = state.spectralMode != 0u ? evalSingleWavelengthBSDF(state, directionSample.wi)
                                             : evalBSDF(state, directionSample.wi);
    if (eval.pdf <= 0.0) return sample;

    sample.pdf = eval.pdf;
    sample.weight = eval.value * absCosTheta(directionSample.wi) / sample.pdf;
    return sample;
}

#endif
