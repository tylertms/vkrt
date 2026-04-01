#ifndef VKRT_INTEGRATOR_PATH_TRANSPORT_COMMON_SLANG
#define VKRT_INTEGRATOR_PATH_TRANSPORT_COMMON_SLANG

void clearSampleDirection(out uint sampleIsTransmission, out float3 sampleWi) {
    sampleIsTransmission = 0u;
    sampleWi = float3(0.0);
}

bool sampleScalarDirection(
    BSDFState state,
    ShadingBasis basis,
    inout uint rng,
    out BSDFSample sample,
    out uint sampleIsTransmission,
    out float3 sampleWi
) {
    sample = sampleBSDF(state, basis, rng);
    if (!sample.isUsable()) {
        clearSampleDirection(sampleIsTransmission, sampleWi);
        return false;
    }

    sampleIsTransmission = sample.isTransmission;
    sampleWi = sample.wi;
    return true;
}

#endif
