#ifndef VKRT_INTEGRATOR_PATH_MODES_RGB_TRANSPORT_SLANG
#define VKRT_INTEGRATOR_PATH_MODES_RGB_TRANSPORT_SLANG

#include "../transport_common.slang"

void accumulateRgbEnvironment(inout RgbSampleState sampleState, RgbPathState pathState, float3 environmentRadiance) {
    sampleState.radiance += pathState.throughput * environmentRadiance;
}

bool applyRgbMediumTransmittance(inout RgbPathState pathState, float hitDistance) {
    if (!pathState.common.medium.absorptionActive()) {
        return any(pathState.throughput > 0.0);
    }

    pathState.throughput *= mediumTransmittance(pathState.common.medium, hitDistance);
    return any(pathState.throughput > 0.0);
}

void accumulateRgbIncomingContribution(inout RgbSampleState sampleState, RgbPathState pathState, float3 contribution) {
    sampleState.radiance += pathState.throughput * contribution;
}

bool sampleRgbNextDirection(
    inout RgbPathState pathState,
    BSDFState state,
    ShadingBasis basis,
    out uint sampleIsTransmission,
    out float3 sampleWi
) {
    BSDFSample sample = BSDFSample();
    if (!sampleScalarDirection(state, basis, pathState.common.rng, sample, sampleIsTransmission, sampleWi)) {
        return false;
    }

    pathState.throughput *= sample.weight;
    if (!any(pathState.throughput > 0.0)) {
        clearSampleDirection(sampleIsTransmission, sampleWi);
        return false;
    }

    pathState.common.prevBsdfPdf = sample.pdf;
    sampleIsTransmission = sample.isTransmission;
    sampleWi = sample.wi;
    return true;
}

void updateRgbMediumAfterScatter(
    inout RgbPathState pathState,
    BSDFMaterial material,
    uint frontFace,
    uint sampleIsTransmission
) {
    updateMediumStateFromTransmission(material, frontFace, sampleIsTransmission, 0.0, 0u, pathState.common.medium);
}

float computeRgbContinueProbability(RgbPathState pathState) {
    return clamp(maxComponent(pathState.throughput), VKRT_RR_MIN_CONTINUE_PROB, VKRT_RR_MAX_CONTINUE_PROB);
}

void applyRgbContinueProbability(inout RgbPathState pathState, float continueProbability) {
    pathState.throughput /= continueProbability;
}

void accumulateResolvedRgbSample(inout RaygenFrameState frameState, RgbSampleState sampleState) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        return;
    }

    frameState.radiance += sampleState.radiance;
    accumulateResolvedSampleFeatures(
        frameState,
        sampleState.albedo,
        sampleState.normal,
        sampleState.featureWeight,
        sampleState.featureDepth,
        sampleState.followSpecular
    );
}

#endif
