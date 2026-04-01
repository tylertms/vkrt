#ifndef VKRT_INTEGRATOR_PATH_LOOP_SLANG
#define VKRT_INTEGRATOR_PATH_LOOP_SLANG

bool shouldAccumulateEnvironmentMiss(RaygenModeState modeState, MediumState medium) {
    return !raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_NEE_ONLY_DEBUG) && !mediumHasActiveBoundary(medium);
}

void recordPrimaryHitInstance(
    inout RaygenPixelState pixelState,
    uint sampleIndex,
    uint depth,
    SceneRayPayload payload
) {
    if (sampleIndex == 0u && depth == 0u) {
        pixelState.firstHitInstance = payload.instanceIndex;
    }
}

SceneRayPayload tracePathSceneRay(inout PathCommonState common, uint depth, uint coherenceHintBits, uint serHintBits) {
    return traceSceneRay(common.ray, depth, coherenceHintBits, serHintBits, common.rng);
}

bool preparePathSurfaceVertex(
    RaygenModeState modeState,
    uint sampleIndex,
    uint depth,
    SceneRayPayload payload,
    RayDesc ray,
    inout RaygenFrameState frameState,
    out PathSurfaceState surfaceState,
    out BSDFMaterial bsdfMaterial
) {
    surfaceState = PathSurfaceState(payload, ray);
    bsdfMaterial = BSDFMaterial(surfaceState.material);
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
        return false;
    }

    return true;
}

BSDFState makePathBSDFState(
    PathCommonState common,
    PathSurfaceState surfaceState,
    BSDFMaterial material,
    float wavelengthNm,
    uint wavelengthMode
) {
    float3 wo = worldToLocal(-common.ray.Direction, surfaceState.basis);
    return BSDFState(material, wo, surfaceState.surface.frontFace, wavelengthNm, wavelengthMode);
}

float bsdfEmitterMisWeight(
    RaygenModeState modeState,
    PathCommonState common,
    SceneRayPayload payload,
    PathSurfaceState surfaceState,
    uint depth
) {
    if (!raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_NEE_ENABLED) || !pathPrevVertexNeeAllowed(common) ||
        depth == 0u || common.prevBsdfPdf <= 0.0) {
        return 1.0;
    }

    return computeBSDFEmitterMISWeight(
        common.prevBsdfPdf,
        meshInfos[payload.instanceIndex].lightPdfArea,
        surfaceState.surface.geometricNormal,
        common.ray.Direction,
        payload.hitDistance
    );
}

void resolveDenoiserFeatureState(
    inout float3 albedo,
    inout float3 normal,
    inout float featureWeight,
    inout float featureDepth,
    inout float followSpecular,
    inout uint flags,
    BSDFMaterial material,
    PathSurfaceState surfaceState,
    uint depth
) {
    bool shouldFollowSpecularHit = materialDenoiserShouldFollowSpecularHit(material, surfaceState.surface.frontFace);
    if (depth == 0u) {
        followSpecular = shouldFollowSpecularHit ? 1.0 : 0.0;
    }

    if (raygenSampleFeaturesResolved(flags) || shouldFollowSpecularHit) {
        return;
    }

    albedo = materialDenoiserAlbedo(material);
    normal = surfaceState.surface.shadingNormal;
    featureWeight = 1.0;
    featureDepth = float(depth + 1u);
    setRaygenSampleFeaturesResolved(flags);
}

void resolveSampleDenoiserFeatures(
    inout RgbSampleState sampleState,
    BSDFMaterial material,
    PathSurfaceState surfaceState,
    uint depth
) {
    resolveDenoiserFeatureState(
        sampleState.albedo,
        sampleState.normal,
        sampleState.featureWeight,
        sampleState.featureDepth,
        sampleState.followSpecular,
        sampleState.flags,
        material,
        surfaceState,
        depth
    );
}

void resolveSampleDenoiserFeatures(
    inout SpectralSingleSampleState sampleState,
    BSDFMaterial material,
    PathSurfaceState surfaceState,
    uint depth
) {
    resolveDenoiserFeatureState(
        sampleState.albedo,
        sampleState.normal,
        sampleState.featureWeight,
        sampleState.featureDepth,
        sampleState.followSpecular,
        sampleState.flags,
        material,
        surfaceState,
        depth
    );
}

void resolveSampleDenoiserFeatures(
    inout SpectralHeroSampleState sampleState,
    BSDFMaterial material,
    PathSurfaceState surfaceState,
    uint depth
) {
    resolveDenoiserFeatureState(
        sampleState.albedo,
        sampleState.normal,
        sampleState.featureWeight,
        sampleState.featureDepth,
        sampleState.followSpecular,
        sampleState.flags,
        material,
        surfaceState,
        depth
    );
}

void accumulateResolvedSampleFeatures(
    inout RaygenFrameState frameState,
    float3 albedo,
    float3 normal,
    float featureWeight,
    float featureDepth,
    float followSpecular
) {
    frameState.albedo += albedo;
    frameState.normal += normal;
    frameState.featureWeight += featureWeight;
    frameState.featureDepth += featureDepth;
    frameState.followSpecular += followSpecular;
}

void advancePathRay(inout PathCommonState common, PathSurfaceState surfaceState, uint sampleIsTransmission, float3 wi) {
    float3 offsetDirection =
        sampleIsTransmission != 0u ? -surfaceState.surface.geometricNormal : surfaceState.surface.geometricNormal;
    common.ray.Origin = surfaceState.hitPoint + offsetDirection * VKRT_SHADOW_ORIGIN_OFFSET;
    common.ray.Direction = wi;
    common.ray.TMin = VKRT_RAY_T_MIN;
    common.ray.TMax = VKRT_RAY_T_MAX;
}

void applyBounceCountDebug(
    RaygenModeState modeState,
    uint sampleIndex,
    uint bounceCount,
    inout RaygenFrameState frameState
) {
    if (!raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_BOUNCE_COUNT_DEBUG) || sampleIndex != 0u) {
        return;
    }

    float t = clamp(float(bounceCount) / max(float(scene.rrMaxDepth), 1.0), 0.0, 1.0);
    frameState.radiance = float3(t, 1.0 - t, 0.0);
    setRaygenFrameDebugEarlyOut(frameState);
}

#endif
