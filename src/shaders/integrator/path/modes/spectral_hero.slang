#ifndef VKRT_INTEGRATOR_PATH_MODES_SPECTRAL_HERO_SLANG
#define VKRT_INTEGRATOR_PATH_MODES_SPECTRAL_HERO_SLANG

#include "../common.slang"

uint bsdfSpectralMode(SpectralHeroPathMode mode) {
    return 1u;
}

void accumulateEnvironment(
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

bool applyMediumTransmittance(inout SpectralHeroPathState pathState, float hitDistance) {
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

void accumulateIncomingContribution(
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

bool sampleNextDirection(
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

    BSDFSample sample = sampleBSDF(state, basis, pathState.common.rng);
    if (!sample.isUsable()) {
        sampleIsTransmission = 0u;
        sampleWi = float3(0.0);
        return false;
    }

    pathState.spectralThroughputScalar *= sample.weight.x;
    if (pathState.spectralThroughputScalar <= 0.0) {
        sampleIsTransmission = 0u;
        sampleWi = float3(0.0);
        return false;
    }

    pathState.common.prevBsdfPdf = sample.pdf;
    sampleIsTransmission = sample.isTransmission;
    sampleWi = sample.wi;
    return true;
}

void updateMediumAfterScatter(
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

float computeContinueProbability(SpectralHeroPathState pathState) {
    return spectralHeroPathActive(pathState)
             ? clamp(maxComponent4(pathState.spectralThroughput), VKRT_RR_MIN_CONTINUE_PROB, VKRT_RR_MAX_CONTINUE_PROB)
             : clamp(pathState.spectralThroughputScalar, VKRT_RR_MIN_CONTINUE_PROB, VKRT_RR_MAX_CONTINUE_PROB);
}

void applyContinueProbability(inout SpectralHeroPathState pathState, float continueProbability) {
    if (spectralHeroPathActive(pathState)) {
        pathState.techniquePathPdf *= continueProbability.xxxx;
        pathState.spectralThroughput /= continueProbability;
        return;
    }

    pathState.spectralThroughputScalar /= continueProbability;
}

void accumulateResolvedSample(
    inout RaygenFrameState frameState,
    SpectralHeroSampleState sampleState,
    SpectralHeroPathState pathState,
    SpectralHeroPathMode mode
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
    frameState.albedo += sampleState.albedo;
    frameState.normal += sampleState.normal;
    frameState.featureWeight += sampleState.featureWeight;
    frameState.featureDepth += sampleState.featureDepth;
    frameState.followSpecular += sampleState.followSpecular;
}

void tracePathSample(
    inout RaygenPixelState pixelState,
    RaygenModeState modeState,
    uint sampleIndex,
    inout RaygenFrameState frameState,
    SpectralHeroPathMode pathMode
) {
    SpectralHeroSampleState sampleState = SpectralHeroSampleState();
    SpectralHeroPathState pathState = SpectralHeroPathState(pixelState, sampleIndex, pathMode);

    for (uint depth = 0u; depth < scene.rrMaxDepth; depth++) {
        Payload payload = Payload(pathState.common.rng);
        traceSceneRay(payload, pathState.common.ray, depth, buildSceneRayCoherenceHint(pathState), kSceneSerHintBits);
        pathState.common.rng = payload.rng;

        if (sampleIndex == 0u && depth == 0u) {
            pixelState.firstHitInstance = payload.instanceIndex;
        }

        if (!payload.hit()) {
            if (!raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_NEE_ONLY_DEBUG) &&
                !mediumHasActiveBoundary(pathState.common.medium)) {
                accumulateEnvironment(sampleState, pathState, sampleEnvironmentRadiance(pathState.common.ray.Direction));
            }
            break;
        }

        if (!applyMediumTransmittance(pathState, payload.hitDistance)) {
            break;
        }

        PathSurfaceState surfaceState = PathSurfaceState(payload, pathState.common.ray);
        if (handlePrimarySurfaceDebug(
                modeState,
                sampleIndex,
                depth,
                surfaceState.material,
                surfaceState.surface.textureData,
                surfaceState.surface.shadingNormal,
                payload.hitDistance,
                frameState
            )) {
            break;
        }

        BSDFMaterial bsdfMaterial = BSDFMaterial(surfaceState.material);
        bool shouldFollowSpecularHit =
            materialDenoiserShouldFollowSpecularHit(bsdfMaterial, surfaceState.surface.frontFace);
        if (depth == 0u) {
            sampleState.followSpecular = shouldFollowSpecularHit ? 1.0 : 0.0;
        }
        if (!raygenSampleFeaturesResolved(sampleState.flags) && !shouldFollowSpecularHit) {
            sampleState.albedo = materialDenoiserAlbedo(bsdfMaterial);
            sampleState.normal = surfaceState.surface.shadingNormal;
            sampleState.featureWeight = 1.0;
            sampleState.featureDepth = float(depth + 1u);
            setRaygenSampleFeaturesResolved(sampleState.flags);
        }

        float3 wo = worldToLocal(-pathState.common.ray.Direction, surfaceState.basis);
        float stateWavelengthNm =
            spectralHeroPathActive(pathState) ? pathState.wavelengthsNm.x : pathState.scalarWavelength.lambdaNm;
        BSDFState state =
            BSDFState(bsdfMaterial, wo, surfaceState.surface.frontFace, stateWavelengthNm, bsdfSpectralMode(pathMode));
        bool currentVertexNeeAllowed = !pathState.common.medium.refractiveActive();
        bool vertexNeeSupported = currentVertexNeeAllowed;

        float spectralContributionScalar = 0.0;
        float4 spectralContribution = float4(0.0);
        if (!raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_BSDF_ONLY_DEBUG) &&
            raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_NEE_ENABLED) && currentVertexNeeAllowed) {
            if (spectralHeroPathActive(pathState)) {
                DirectLightHeroResult directLight = sampleDirectLightSpectralHero(
                    surfaceState.hitPoint,
                    surfaceState.surface.geometricNormal,
                    surfaceState.basis,
                    state,
                    pathState.common.medium,
                    pathState.techniquePathPdf,
                    pathState.wavelengthsNm,
                    pathState.common.rng
                );
                spectralContribution += directLight.spectralRadiance;
                vertexNeeSupported = directLight.neeSupported();
            } else {
                DirectLightSpectralResult directLight = sampleDirectLightSpectralSingle(
                    surfaceState.hitPoint,
                    surfaceState.surface.geometricNormal,
                    surfaceState.basis,
                    state,
                    pathState.common.medium,
                    pathState.common.rng
                );
                spectralContributionScalar += directLight.spectralRadiance;
                vertexNeeSupported = directLight.neeSupported();
            }
        }

        if (!raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_NEE_ONLY_DEBUG)) {
            float3 emission = surfaceState.material.emissionColor * surfaceState.material.emissionLuminance;
            if (any(emission > 0.0)) {
                float misWeight = 1.0;
                if (spectralHeroPathActive(pathState)) {
                    misWeight = heroWavelengthBalanceWeight(pathState.techniquePathPdf);
                    if (raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_NEE_ENABLED) &&
                        pathPrevVertexNeeAllowed(pathState.common) && depth > 0u) {
                        float lightPdfSolidAngle = lightPdfAreaToSolidAngle(
                            meshInfos[payload.instanceIndex].lightPdfArea,
                            surfaceState.surface.geometricNormal,
                            pathState.common.ray.Direction,
                            payload.hitDistance
                        );
                        misWeight = computeSpectralEmitterMISWeight(
                            pathState.prevVertexTechniquePathPdf,
                            pathState.prevBsdfTechniquePdf,
                            lightPdfSolidAngle
                        );
                    }
                    spectralContribution +=
                        spectralScalarFromLinearSrgb4(emission, pathState.wavelengthsNm) * misWeight;
                } else {
                    if (raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_NEE_ENABLED) &&
                        pathPrevVertexNeeAllowed(pathState.common) && depth > 0u &&
                        pathState.common.prevBsdfPdf > 0.0) {
                        misWeight = computeBSDFEmitterMISWeight(
                            pathState.common.prevBsdfPdf,
                            meshInfos[payload.instanceIndex].lightPdfArea,
                            surfaceState.surface.geometricNormal,
                            pathState.common.ray.Direction,
                            payload.hitDistance
                        );
                    }
                    spectralContributionScalar +=
                        spectralScalarFromLinearSrgb(emission, pathState.scalarWavelength.lambdaNm) * misWeight;
                }
            }
        }

        accumulateIncomingContribution(sampleState, pathState, spectralContributionScalar, spectralContribution);
        pathState.common.bounceCount = depth + 1u;

        uint sampleIsTransmission = 0u;
        float3 sampleWi = float3(0.0);
        if (!sampleNextDirection(
                sampleState,
                pathState,
                state,
                surfaceState.basis,
                bsdfMaterial,
                sampleIsTransmission,
                sampleWi
            )) {
            break;
        }

        bool sampledPathNeeAllowed = vertexNeeSupported;
        if (sampledPathNeeAllowed && sampleIsTransmission != 0u) {
            sampledPathNeeAllowed = false;
        }

        updateMediumAfterScatter(pathState, bsdfMaterial, surfaceState.surface.frontFace, sampleIsTransmission);
        setPathPrevVertexNeeAllowed(pathState.common, sampledPathNeeAllowed);

        if (depth + 1u >= scene.rrMinDepth) {
            float continueProbability = computeContinueProbability(pathState);
            if (rand(pathState.common.rng) > continueProbability) {
                break;
            }
            applyContinueProbability(pathState, continueProbability);
        }

        float3 offsetDir =
            sampleIsTransmission != 0u ? -surfaceState.surface.geometricNormal : surfaceState.surface.geometricNormal;
        pathState.common.ray.Origin = surfaceState.hitPoint + offsetDir * VKRT_SHADOW_ORIGIN_OFFSET;
        pathState.common.ray.Direction = sampleWi;
        pathState.common.ray.TMin = VKRT_RAY_T_MIN;
        pathState.common.ray.TMax = VKRT_RAY_T_MAX;
    }

    if (raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_BOUNCE_COUNT_DEBUG) && sampleIndex == 0u) {
        float t = clamp(float(pathState.common.bounceCount) / max(float(scene.rrMaxDepth), 1.0), 0.0, 1.0);
        frameState.radiance = float3(t, 1.0 - t, 0.0);
        setRaygenFrameDebugEarlyOut(frameState);
    }

    accumulateResolvedSample(frameState, sampleState, pathState, pathMode);
}

#endif
