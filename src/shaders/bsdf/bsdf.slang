#ifndef VKRT_BSDF_BSDF_SLANG
#define VKRT_BSDF_BSDF_SLANG

#include "model.slang"

BSDFSample sampleBSDF(BSDFState state, ShadingBasis basis, inout uint rng) {
    BSDFSample sample = BSDFSample();
    if (absCosTheta(state.wo) <= 0.0) return sample;

    BSDFDirectionSample directionSample;
    if (!sampleBSDFDirection(state, rng, directionSample)) {
        return sample;
    }

    sample.isTransmission = directionSample.isTransmission;
    sample.wi = localToWorld(directionSample.wi, basis);

    BSDFEval eval = evalBSDF(state, directionSample.wi);
    if (eval.pdf <= 0.0) {
        return sample;
    }

    sample.pdf = eval.pdf;
    sample.weight = eval.value * absCosTheta(directionSample.wi) / sample.pdf;

    return sample;
}

#endif
