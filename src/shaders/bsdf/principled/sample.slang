#ifndef VKRT_BSDF_PRINCIPLED_SAMPLE_SLANG
#define VKRT_BSDF_PRINCIPLED_SAMPLE_SLANG

bool sampleBSDFDirection(BSDFState state, inout uint rng, out BSDFDirectionSample sample) {
    sample = {};

    float selector = rand(rng);
    if (selector < state.sampleWeights.sheen) {
        return sampleSheen(state.wo, state.material.sheenRoughness, rng, sample.wi);
    }

    selector -= state.sampleWeights.sheen;
    if (selector < state.sampleWeights.coat) {
        return sampleClearcoat(state.wo, state.clearcoat, rng, sample.wi);
    }

    selector -= state.sampleWeights.coat;
    if (selector < state.sampleWeights.metal) {
        return sampleGGX(state.wo, state.ggx, rng, sample.wi);
    }

    selector -= state.sampleWeights.metal;
    if (selector < state.sampleWeights.dielectric) {
        DielectricSample dielectricSample;
        if (!sampleDielectric(
                state.material,
                state.wo,
                state.frontFace,
                state.ggx,
                state.wavelengthNm,
                state.spectralMode,
                rng,
                dielectricSample
            )) {
            return false;
        }

        sample.wi = dielectricSample.wi;
        sample.isTransmission = dielectricSample.isTransmission;
        return true;
    }

    selector -= state.sampleWeights.dielectric;
    if (selector < state.sampleWeights.diffuse) {
        sample.wi = sampleCosineHemisphere(rng);
        return true;
    }

    sample.wi = sampleCosineHemisphere(rng);
    return true;
}

#endif
