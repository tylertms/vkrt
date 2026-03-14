#ifndef VKRT_BSDF_BSDF_SLANG
#define VKRT_BSDF_BSDF_SLANG

#include "model.slang"

BSDFSample sampleBSDF(BSDFInput input, inout uint rng) {
    BSDFSample sample = BSDFSample();

    ShadingBasis basis = makeShadingBasis(input.normal);
    float3 wo = worldToLocal(input.wo, basis);
    BSDFState state = makeBSDFState(input.material, wo);
    if (state.wo.z <= 0.0) return sample;

    float3 wi;
    if (!sampleBSDFDirection(state, rng, wi)) {
        return sample;
    }

    BSDFEval eval = evalBSDF(state, wi);
    if (eval.pdf <= 0.0) {
        return sample;
    }

    sample.value = eval.value;
    sample.pdf = eval.pdf;
    sample.wi = localToWorld(wi, basis);
    sample.weight = sample.value * wi.z / sample.pdf;
    return sample;
}

#endif
