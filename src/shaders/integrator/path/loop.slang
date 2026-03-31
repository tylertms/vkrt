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

void resolveSampleDenoiserFeatures(
    inout RgbSampleState sampleState,
    BSDFMaterial material,
    PathSurfaceState surfaceState,
    uint depth
) {
    bool shouldFollowSpecularHit = materialDenoiserShouldFollowSpecularHit(material, surfaceState.surface.frontFace);
    if (depth == 0u) {
        sampleState.followSpecular = shouldFollowSpecularHit ? 1.0 : 0.0;
    }

    if (raygenSampleFeaturesResolved(sampleState.flags) || shouldFollowSpecularHit) {
        return;
    }

    sampleState.albedo = materialDenoiserAlbedo(material);
    sampleState.normal = surfaceState.surface.shadingNormal;
    sampleState.featureWeight = 1.0;
    sampleState.featureDepth = float(depth + 1u);
    setRaygenSampleFeaturesResolved(sampleState.flags);
}

void resolveSampleDenoiserFeatures(
    inout SpectralSingleSampleState sampleState,
    BSDFMaterial material,
    PathSurfaceState surfaceState,
    uint depth
) {
    bool shouldFollowSpecularHit = materialDenoiserShouldFollowSpecularHit(material, surfaceState.surface.frontFace);
    if (depth == 0u) {
        sampleState.followSpecular = shouldFollowSpecularHit ? 1.0 : 0.0;
    }

    if (raygenSampleFeaturesResolved(sampleState.flags) || shouldFollowSpecularHit) {
        return;
    }

    sampleState.albedo = materialDenoiserAlbedo(material);
    sampleState.normal = surfaceState.surface.shadingNormal;
    sampleState.featureWeight = 1.0;
    sampleState.featureDepth = float(depth + 1u);
    setRaygenSampleFeaturesResolved(sampleState.flags);
}

void resolveSampleDenoiserFeatures(
    inout SpectralHeroSampleState sampleState,
    BSDFMaterial material,
    PathSurfaceState surfaceState,
    uint depth
) {
    bool shouldFollowSpecularHit = materialDenoiserShouldFollowSpecularHit(material, surfaceState.surface.frontFace);
    if (depth == 0u) {
        sampleState.followSpecular = shouldFollowSpecularHit ? 1.0 : 0.0;
    }

    if (raygenSampleFeaturesResolved(sampleState.flags) || shouldFollowSpecularHit) {
        return;
    }

    sampleState.albedo = materialDenoiserAlbedo(material);
    sampleState.normal = surfaceState.surface.shadingNormal;
    sampleState.featureWeight = 1.0;
    sampleState.featureDepth = float(depth + 1u);
    setRaygenSampleFeaturesResolved(sampleState.flags);
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
