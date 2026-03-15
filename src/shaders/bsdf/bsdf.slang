#ifndef VKRT_BSDF_BSDF_SLANG
#define VKRT_BSDF_BSDF_SLANG

#include "model.slang"

BSDFSample sampleBSDF(BSDFInput input, inout uint rng) {
    BSDFSample sample = BSDFSample();

    ShadingBasis basis = makeShadingBasis(input.normal, input.tangent);
    float3 wo = worldToLocal(input.wo, basis);
    BSDFState state = makeBSDFState(input.material, wo, input.frontFace);
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

    sample.value = eval.value;
    sample.pdf = eval.pdf;
    sample.weight = sample.value * absCosTheta(directionSample.wi) / sample.pdf;
    return sample;
}

#endif
