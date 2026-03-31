#ifndef VKRT_INTEGRATOR_PATH_MODES_RGB_SLANG
#define VKRT_INTEGRATOR_PATH_MODES_RGB_SLANG

#include "../common.slang"

uint bsdfSpectralMode(RgbPathMode mode) {
    return 0u;
}

void accumulateEnvironment(inout RgbSampleState sampleState, RgbPathState pathState, float3 environmentRadiance) {
    sampleState.radiance += pathState.throughput * environmentRadiance;
}

bool applyMediumTransmittance(inout RgbPathState pathState, float hitDistance) {
    if (!pathState.common.medium.absorptionActive()) {
        return any(pathState.throughput > 0.0);
    }

    pathState.throughput *= mediumTransmittance(pathState.common.medium, hitDistance);
    return any(pathState.throughput > 0.0);
}

void accumulateIncomingContribution(inout RgbSampleState sampleState, RgbPathState pathState, float3 contribution) {
    sampleState.radiance += pathState.throughput * contribution;
}

bool sampleNextDirection(
    inout RgbPathState pathState,
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

    pathState.throughput *= sample.weight;
    if (!any(pathState.throughput > 0.0)) {
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
    inout RgbPathState pathState,
    BSDFMaterial material,
    uint frontFace,
    uint sampleIsTransmission
) {
    updateMediumStateFromTransmission(material, frontFace, sampleIsTransmission, 0.0, 0u, pathState.common.medium);
}

float computeContinueProbability(RgbPathState pathState) {
    return clamp(maxComponent(pathState.throughput), VKRT_RR_MIN_CONTINUE_PROB, VKRT_RR_MAX_CONTINUE_PROB);
}

void applyContinueProbability(inout RgbPathState pathState, float continueProbability) {
    pathState.throughput /= continueProbability;
}

void accumulateResolvedSample(inout RaygenFrameState frameState, RgbSampleState sampleState) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        return;
    }

    frameState.radiance += sampleState.radiance;
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
    RgbPathMode pathMode
) {
    RgbSampleState sampleState = RgbSampleState();
    RgbPathState pathState = RgbPathState(pixelState, sampleIndex, pathMode);

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
        BSDFState state = BSDFState(bsdfMaterial, wo, surfaceState.surface.frontFace, 0.0, bsdfSpectralMode(pathMode));
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

        accumulateIncomingContribution(sampleState, pathState, contribution);
        pathState.common.bounceCount = depth + 1u;

        uint sampleIsTransmission = 0u;
        float3 sampleWi = float3(0.0);
        if (!sampleNextDirection(pathState, state, surfaceState.basis, sampleIsTransmission, sampleWi)) {
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

    accumulateResolvedSample(frameState, sampleState);
}

#endif
