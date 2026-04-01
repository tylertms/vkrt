#ifndef VKRT_INTEGRATOR_PATH_MODES_SPECTRAL_HERO_TRANSPORT_SLANG
#define VKRT_INTEGRATOR_PATH_MODES_SPECTRAL_HERO_TRANSPORT_SLANG

#include "../transport_common.slang"

void accumulateSpectralHeroEnvironment(
    inout SpectralHeroSampleState sampleState,
    SpectralHeroPathState pathState,
    float3 environmentRadiance
) {
    if (spectralHeroPathActive(pathState)) {
        sampleState.spectralRadiance += pathState.spectralThroughput *
                                        heroWavelengthBalanceWeight(pathState.techniquePathPdf) *
                                        spectralScalarFromLinearSrgb4(environmentRadiance, pathState.wavelengthsNm);
    } else {
        sampleState.spectralRadianceScalar +=
            pathState.spectralThroughputScalar *
            spectralScalarFromLinearSrgb(environmentRadiance, pathState.scalarWavelength.lambdaNm);
    }
}

bool applySpectralHeroMediumTransmittance(inout SpectralHeroPathState pathState, float hitDistance) {
    if (!pathState.common.medium.absorptionActive()) {
        return spectralHeroPathActive(pathState) ? any(pathState.spectralThroughput > 0.0)
                                                 : pathState.spectralThroughputScalar > 0.0;
    }

    if (spectralHeroPathActive(pathState)) {
        pathState.spectralThroughput *= mediumSpectralTransmittance(pathState.common.medium, hitDistance);
        return any(pathState.spectralThroughput > 0.0);
    }

    pathState.spectralThroughputScalar *= mediumTransmittance(pathState.common.medium, hitDistance).x;
    return pathState.spectralThroughputScalar > 0.0;
}

void accumulateSpectralHeroIncomingContribution(
    inout SpectralHeroSampleState sampleState,
    SpectralHeroPathState pathState,
    float spectralContributionScalar,
    float4 spectralContribution
) {
    if (spectralHeroPathActive(pathState)) {
        sampleState.spectralRadiance += pathState.spectralThroughput * spectralContribution;
    } else {
        sampleState.spectralRadianceScalar += pathState.spectralThroughputScalar * spectralContributionScalar;
    }
}

bool sampleSpectralHeroNextDirection(
    inout SpectralHeroSampleState sampleState,
    inout SpectralHeroPathState pathState,
    BSDFState state,
    ShadingBasis basis,
    BSDFMaterial material,
    out uint sampleIsTransmission,
    out float3 sampleWi
) {
    if (spectralHeroPathActive(pathState)) {
        SpectralBSDFSample sample = sampleSpectralBSDF(state, basis, pathState.wavelengthsNm, pathState.common.rng);
        if (!sample.isUsable()) {
            sampleIsTransmission = 0u;
            sampleWi = float3(0.0);
            return false;
        }

        pathState.prevVertexTechniquePathPdf = pathState.techniquePathPdf;
        pathState.prevBsdfTechniquePdf = sample.techniquePdf;
        pathState.techniquePathPdf *= sample.techniquePdf;
        pathState.spectralThroughput *= sample.weight;
        if (!any(pathState.spectralThroughput > 0.0)) {
            sampleIsTransmission = 0u;
            sampleWi = float3(0.0);
            return false;
        }

        if (sample.isTransmission != 0u && materialMediumIsRefractive(material) && material.abbeNumber > 0.0) {
            sampleState.radiance +=
                spectralSample4ToXYZ(sampleState.spectralRadiance, pathState.wavelengthsNm, pathState.invWavelengthPdf);
            sampleState.spectralRadiance = float4(0.0);
            pathState.spectralThroughputScalar = pathState.spectralThroughput.x;
            pathState.spectralThroughput = float4(0.0);
            pathState.scalarWavelength.lambdaNm = pathState.wavelengthsNm.x;
            pathState.scalarWavelength.invPdf = pathState.invWavelengthPdf.x;
            pathState.common.prevBsdfPdf = sample.techniquePdf.x;
            setSpectralHeroPathActive(pathState, false);
        }

        sampleIsTransmission = sample.isTransmission;
        sampleWi = sample.wi;
        return true;
    }

    BSDFSample sample = BSDFSample();
    if (!sampleScalarDirection(state, basis, pathState.common.rng, sample, sampleIsTransmission, sampleWi)) {
        return false;
    }

    pathState.spectralThroughputScalar *= sample.weight.x;
    if (pathState.spectralThroughputScalar <= 0.0) {
        clearSampleDirection(sampleIsTransmission, sampleWi);
        return false;
    }

    pathState.common.prevBsdfPdf = sample.pdf;
    sampleIsTransmission = sample.isTransmission;
    sampleWi = sample.wi;
    return true;
}

void updateSpectralHeroMediumAfterScatter(
    inout SpectralHeroPathState pathState,
    BSDFMaterial material,
    uint frontFace,
    uint sampleIsTransmission
) {
    if (spectralHeroPathActive(pathState)) {
        updateMediumStateFromTransmissionSpectral(
            material,
            frontFace,
            sampleIsTransmission,
            pathState.wavelengthsNm,
            pathState.common.medium
        );
        return;
    }

    updateMediumStateFromTransmission(
        material,
        frontFace,
        sampleIsTransmission,
        pathState.scalarWavelength.lambdaNm,
        1u,
        pathState.common.medium
    );
}

float computeSpectralHeroContinueProbability(SpectralHeroPathState pathState) {
    return spectralHeroPathActive(pathState)
             ? clamp(maxComponent4(pathState.spectralThroughput), VKRT_RR_MIN_CONTINUE_PROB, VKRT_RR_MAX_CONTINUE_PROB)
             : clamp(pathState.spectralThroughputScalar, VKRT_RR_MIN_CONTINUE_PROB, VKRT_RR_MAX_CONTINUE_PROB);
}

void applySpectralHeroContinueProbability(inout SpectralHeroPathState pathState, float continueProbability) {
    if (spectralHeroPathActive(pathState)) {
        pathState.techniquePathPdf *= continueProbability.xxxx;
        pathState.spectralThroughput /= continueProbability;
        return;
    }

    pathState.spectralThroughputScalar /= continueProbability;
}

void accumulateResolvedSpectralHeroSample(
    inout RaygenFrameState frameState,
    SpectralHeroSampleState sampleState,
    SpectralHeroPathState pathState
) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        return;
    }

    float3 sampleRadiance = sampleState.radiance;
    if (spectralHeroPathActive(pathState)) {
        sampleRadiance +=
            spectralSample4ToXYZ(sampleState.spectralRadiance, pathState.wavelengthsNm, pathState.invWavelengthPdf);
    } else {
        sampleRadiance += spectralSampleToXYZ(sampleState.spectralRadianceScalar, pathState.scalarWavelength);
    }

    frameState.radiance += sampleRadiance;
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
