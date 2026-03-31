#ifndef VKRT_INTEGRATOR_PATH_MODES_SPECTRAL_SINGLE_TRANSPORT_SLANG
#define VKRT_INTEGRATOR_PATH_MODES_SPECTRAL_SINGLE_TRANSPORT_SLANG

void accumulateSpectralSingleEnvironment(
    inout SpectralSingleSampleState sampleState,
    SpectralSinglePathState pathState,
    float3 environmentRadiance
) {
    sampleState.spectralRadiance +=
        pathState.spectralThroughput * spectralScalarFromLinearSrgb(environmentRadiance, pathState.wavelength.lambdaNm);
}

bool applySpectralSingleMediumTransmittance(inout SpectralSinglePathState pathState, float hitDistance) {
    if (!pathState.common.medium.absorptionActive()) {
        return pathState.spectralThroughput > 0.0;
    }

    pathState.spectralThroughput *= mediumTransmittance(pathState.common.medium, hitDistance).x;
    return pathState.spectralThroughput > 0.0;
}

void accumulateSpectralSingleIncomingContribution(
    inout SpectralSingleSampleState sampleState,
    SpectralSinglePathState pathState,
    float spectralContribution
) {
    sampleState.spectralRadiance += pathState.spectralThroughput * spectralContribution;
}

bool sampleSpectralSingleNextDirection(
    inout SpectralSinglePathState pathState,
    BSDFState state,
    ShadingBasis basis,
    out uint sampleIsTransmission,
    out float3 sampleWi
) {
    BSDFSample sample = sampleBSDF(state, basis, pathState.common.rng);
    if (!sample.isUsable()) {
        sampleIsTransmission = 0u;
        sampleWi = float3(0.0);
        return false;
    }

    pathState.spectralThroughput *= sample.weight.x;
    if (pathState.spectralThroughput <= 0.0) {
        sampleIsTransmission = 0u;
        sampleWi = float3(0.0);
        return false;
    }

    pathState.common.prevBsdfPdf = sample.pdf;
    sampleIsTransmission = sample.isTransmission;
    sampleWi = sample.wi;
    return true;
}

void updateSpectralSingleMediumAfterScatter(
    inout SpectralSinglePathState pathState,
    BSDFMaterial material,
    uint frontFace,
    uint sampleIsTransmission
) {
    updateMediumStateFromTransmission(
        material,
        frontFace,
        sampleIsTransmission,
        pathState.wavelength.lambdaNm,
        1u,
        pathState.common.medium
    );
}

float computeSpectralSingleContinueProbability(SpectralSinglePathState pathState) {
    return clamp(pathState.spectralThroughput, VKRT_RR_MIN_CONTINUE_PROB, VKRT_RR_MAX_CONTINUE_PROB);
}

void applySpectralSingleContinueProbability(inout SpectralSinglePathState pathState, float continueProbability) {
    pathState.spectralThroughput /= continueProbability;
}

void accumulateResolvedSpectralSingleSample(
    inout RaygenFrameState frameState,
    SpectralSingleSampleState sampleState,
    SpectralSinglePathState pathState
) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        return;
    }

    frameState.radiance += spectralSampleToXYZ(sampleState.spectralRadiance, pathState.wavelength);
    frameState.albedo += sampleState.albedo;
    frameState.normal += sampleState.normal;
    frameState.featureWeight += sampleState.featureWeight;
    frameState.featureDepth += sampleState.featureDepth;
    frameState.followSpecular += sampleState.followSpecular;
}

#endif
