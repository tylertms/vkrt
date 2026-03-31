#ifndef VKRT_INTEGRATOR_PATH_RGB_INTEGRATOR_SLANG
#define VKRT_INTEGRATOR_PATH_RGB_INTEGRATOR_SLANG

#include "../../../bsdf/sample_rgb.slang"
#include "../../../light/direct/rgb_lighting.slang"
#include "../../../light/environment.slang"
#include "../../../rt/queries/scene_query.slang"
#include "../coherence.slang"
#include "../debug.slang"
#include "../loop.slang"
#include "../state.slang"
#include "../surface_state.slang"
#include "./transport.slang"

void traceRgbPathSample(
    inout RaygenPixelState pixelState,
    RaygenModeState modeState,
    uint sampleIndex,
    inout RaygenFrameState frameState
) {
    RgbSampleState sampleState = RgbSampleState();
    RgbPathState pathState = RgbPathState(pixelState, sampleIndex);

    for (uint depth = 0u; depth < scene.rrMaxDepth; depth++) {
        SceneRayPayload payload = SceneRayPayload(pathState.common.rng);
        traceSceneRay(
            payload,
            pathState.common.ray,
            depth,
            buildSceneRayCoherenceHint(pathState),
            VKRT_SCENE_SER_HINT_BITS
        );
        pathState.common.rng = payload.rng;
        recordPrimaryHitInstance(pixelState, sampleIndex, depth, payload);

        if (!payload.hit()) {
            if (shouldAccumulateEnvironmentMiss(modeState, pathState.common.medium)) {
                accumulateRgbEnvironment(
                    sampleState,
                    pathState,
                    sampleEnvironmentRadiance(pathState.common.ray.Direction)
                );
            }
            break;
        }

        if (!applyRgbMediumTransmittance(pathState, payload.hitDistance)) {
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
        resolveSampleDenoiserFeatures(sampleState, bsdfMaterial, surfaceState, depth);

        float3 wo = worldToLocal(-pathState.common.ray.Direction, surfaceState.basis);
        BSDFState state = BSDFState(bsdfMaterial, wo, surfaceState.surface.frontFace, 0.0, 0u);
        bool currentVertexNeeAllowed = !pathState.common.medium.refractiveActive();
        bool vertexNeeSupported = currentVertexNeeAllowed;

        float3 contribution = float3(0.0);
        if (!raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_BSDF_ONLY_DEBUG) &&
            raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_NEE_ENABLED) && currentVertexNeeAllowed) {
            DirectLightRgbResult directLight = sampleDirectLightRgb(
                surfaceState.hitPoint,
                surfaceState.surface.geometricNormal,
                surfaceState.basis,
                state,
                pathState.common.medium,
                pathState.common.rng
            );
            contribution += directLight.radiance;
            vertexNeeSupported = directLight.neeSupported();
        }

        if (!raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_NEE_ONLY_DEBUG)) {
            float3 emission = surfaceState.material.emissionColor * surfaceState.material.emissionLuminance;
            if (any(emission > 0.0)) {
                float misWeight = 1.0;
                if (raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_NEE_ENABLED) &&
                    pathPrevVertexNeeAllowed(pathState.common) && depth > 0u && pathState.common.prevBsdfPdf > 0.0) {
                    misWeight = computeBSDFEmitterMISWeight(
                        pathState.common.prevBsdfPdf,
                        meshInfos[payload.instanceIndex].lightPdfArea,
                        surfaceState.surface.geometricNormal,
                        pathState.common.ray.Direction,
                        payload.hitDistance
                    );
                }
                contribution += emission * misWeight;
            }
        }

        accumulateRgbIncomingContribution(sampleState, pathState, contribution);
        pathState.common.bounceCount = depth + 1u;

        uint sampleIsTransmission = 0u;
        float3 sampleWi = float3(0.0);
        if (!sampleRgbNextDirection(pathState, state, surfaceState.basis, sampleIsTransmission, sampleWi)) {
            break;
        }

        bool sampledPathNeeAllowed = vertexNeeSupported && sampleIsTransmission == 0u;
        updateRgbMediumAfterScatter(pathState, bsdfMaterial, surfaceState.surface.frontFace, sampleIsTransmission);
        setPathPrevVertexNeeAllowed(pathState.common, sampledPathNeeAllowed);

        if (depth + 1u >= scene.rrMinDepth) {
            float continueProbability = computeRgbContinueProbability(pathState);
            if (rand(pathState.common.rng) > continueProbability) {
                break;
            }
            applyRgbContinueProbability(pathState, continueProbability);
        }

        advancePathRay(pathState.common, surfaceState, sampleIsTransmission, sampleWi);
    }

    applyBounceCountDebug(modeState, sampleIndex, pathState.common.bounceCount, frameState);
    accumulateResolvedRgbSample(frameState, sampleState);
}

#endif
