#ifndef VKRT_INTEGRATOR_PATH_SPECTRAL_HERO_INTEGRATOR_SLANG
#define VKRT_INTEGRATOR_PATH_SPECTRAL_HERO_INTEGRATOR_SLANG

#include "../../../bsdf/sample_rgb.slang"
#include "../../../bsdf/sample_spectral.slang"
#include "../../../light/direct/spectral_lighting.slang"
#include "../../../light/environment.slang"
#include "../../../rt/queries/scene_query.slang"
#include "../coherence.slang"
#include "../debug.slang"
#include "../loop.slang"
#include "../state.slang"
#include "../surface_state.slang"
#include "./transport.slang"

void traceSpectralHeroPathSample(
    inout RaygenPixelState pixelState,
    RaygenModeState modeState,
    uint sampleIndex,
    inout RaygenFrameState frameState
) {
    SpectralHeroSampleState sampleState = SpectralHeroSampleState();
    SpectralHeroPathState pathState = SpectralHeroPathState(pixelState, sampleIndex);

    for (uint depth = 0u; depth < scene.rrMaxDepth; depth++) {
        SceneRayPayload payload =
            tracePathSceneRay(pathState.common, depth, buildSceneRayCoherenceHint(pathState), VKRT_SCENE_SER_HINT_BITS);
        recordPrimaryHitInstance(pixelState, sampleIndex, depth, payload);

        if (!payload.hit()) {
            if (shouldAccumulateEnvironmentMiss(modeState, pathState.common.medium)) {
                accumulateSpectralHeroEnvironment(
                    sampleState,
                    pathState,
                    sampleEnvironmentRadiance(pathState.common.ray.Direction)
                );
            }
            break;
        }

        if (!applySpectralHeroMediumTransmittance(pathState, payload.hitDistance)) {
            break;
        }

        PathSurfaceState surfaceState;
        BSDFMaterial bsdfMaterial;
        if (!preparePathSurfaceVertex(
                modeState,
                sampleIndex,
                depth,
                payload,
                pathState.common.ray,
                frameState,
                surfaceState,
                bsdfMaterial
            )) {
            break;
        }
        resolveSampleDenoiserFeatures(sampleState, bsdfMaterial, surfaceState, depth);

        float stateWavelengthNm =
            spectralHeroPathActive(pathState) ? pathState.wavelengthsNm.x : pathState.scalarWavelength.lambdaNm;
        BSDFState state = makePathBSDFState(pathState.common, surfaceState, bsdfMaterial, stateWavelengthNm, 1u);
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
                    misWeight = bsdfEmitterMisWeight(modeState, pathState.common, payload, surfaceState, depth);
                    spectralContributionScalar +=
                        spectralScalarFromLinearSrgb(emission, pathState.scalarWavelength.lambdaNm) * misWeight;
                }
            }
        }

        accumulateSpectralHeroIncomingContribution(
            sampleState,
            pathState,
            spectralContributionScalar,
            spectralContribution
        );
        pathState.common.bounceCount = depth + 1u;

        uint sampleIsTransmission = 0u;
        float3 sampleWi = float3(0.0);
        if (!sampleSpectralHeroNextDirection(
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

        bool sampledPathNeeAllowed = vertexNeeSupported && sampleIsTransmission == 0u;
        updateSpectralHeroMediumAfterScatter(
            pathState,
            bsdfMaterial,
            surfaceState.surface.frontFace,
            sampleIsTransmission
        );
        setPathPrevVertexNeeAllowed(pathState.common, sampledPathNeeAllowed);

        if (depth + 1u >= scene.rrMinDepth) {
            float continueProbability = computeSpectralHeroContinueProbability(pathState);
            if (rand(pathState.common.rng) > continueProbability) {
                break;
            }
            applySpectralHeroContinueProbability(pathState, continueProbability);
        }

        advancePathRay(pathState.common, surfaceState, sampleIsTransmission, sampleWi);
    }

    applyBounceCountDebug(modeState, sampleIndex, pathState.common.bounceCount, frameState);
    accumulateResolvedSpectralHeroSample(frameState, sampleState, pathState);
}

#endif
