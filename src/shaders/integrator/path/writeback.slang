#ifndef VKRT_INTEGRATOR_PATH_WRITEBACK_SLANG
#define VKRT_INTEGRATOR_PATH_WRITEBACK_SLANG

#include "common.slang"

void finalizeFrameAccumulation(RaygenPixelState pixelState, inout RaygenFrameState frameState) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        return;
    }

    frameState.radiance /= float(pixelState.spp);
    if (frameState.featureWeight > 0.0) {
        frameState.albedo /= frameState.featureWeight;
        frameState.normal /= frameState.featureWeight;
    } else {
        frameState.albedo = float3(0.0);
        frameState.normal = float3(0.0);
    }
}

void applyFrameDebugOverrides(
    RaygenPixelState pixelState,
    RaygenModeState modeState,
    inout RaygenFrameState frameState
) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        return;
    }

    if (raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_DENOISER_ALBEDO_DEBUG)) {
        frameState.radiance = frameState.albedo;
        setRaygenFrameDebugEarlyOut(frameState);
    } else if (raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_DENOISER_NORMAL_DEBUG)) {
        frameState.radiance = frameState.featureWeight > 0.0 ? frameState.normal * 0.5 + 0.5 : float3(0.0);
        setRaygenFrameDebugEarlyOut(frameState);
    } else if (raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_DENOISER_FEATURE_VALIDITY_DEBUG)) {
        float coverage = saturate(frameState.featureWeight / float(pixelState.spp));
        frameState.radiance = coverage.xxx;
        setRaygenFrameDebugEarlyOut(frameState);
    } else if (raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_DENOISER_FEATURE_DEPTH_DEBUG)) {
        float normalizedDepth = 0.0;
        if (frameState.featureWeight > 0.0) {
            float averageFeatureDepth = frameState.featureDepth / frameState.featureWeight;
            normalizedDepth = saturate((averageFeatureDepth - 1.0) / max(float(scene.rrMaxDepth - 1u), 1.0));
        }
        frameState.radiance = normalizedDepth.xxx;
        setRaygenFrameDebugEarlyOut(frameState);
    } else if (raygenModeHas(modeState, VKRT_RAYGEN_MODE_FLAG_DENOISER_FOLLOW_SPECULAR_DEBUG)) {
        float followRatio = saturate(frameState.followSpecular / float(pixelState.spp));
        frameState.radiance = lerp(float3(0.05), float3(1.0, 0.6, 0.0), followRatio);
        setRaygenFrameDebugEarlyOut(frameState);
    }
}

float3 mapAccumulatedRadiance(float3 accumulated, RgbPathMode mode) {
    return mapSceneColorToDisplay(accumulated);
}

float3 mapAccumulatedRadiance(float3 accumulated, SpectralSinglePathMode mode) {
    return mapSceneColorToDisplay(xyzToLinearSrgb(accumulated));
}

float3 mapAccumulatedRadiance(float3 accumulated, SpectralHeroPathMode mode) {
    return mapAccumulatedRadiance(accumulated, SpectralSinglePathMode());
}

void writeDebugFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        accumulationWriteImage[pixelState.pixel] = float4(frameState.radiance, 0.0);
        albedoWriteImage[pixelState.pixel] = float4(0.0);
        normalWriteImage[pixelState.pixel] = float4(0.0);
        outputImage[pixelState.pixel] = float4(encodeDisplayColor(frameState.radiance), 1.0);
    }
}

void writeAccumulatedFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState, RgbPathMode mode) {
    float4 previousAlbedo = albedoReadImage[pixelState.pixel];
    float4 previousNormal = normalReadImage[pixelState.pixel];
    float previousWeight = float(pixelState.previousSamples);
    float newWeight = float(pixelState.spp);
    float totalWeight = previousWeight + newWeight;
    float previousAlbedoWeight = previousAlbedo.w;
    float previousNormalWeight = previousNormal.w;
    float totalAlbedoWeight = previousAlbedoWeight + frameState.featureWeight;
    float totalNormalWeight = previousNormalWeight + frameState.featureWeight;
    float3 accumulated = (pixelState.previousAccumulation.xyz * previousWeight + frameState.radiance * newWeight) /
                         max(totalWeight, 1.0);
    float3 accumulatedAlbedo =
        totalAlbedoWeight > 0.0
            ? (previousAlbedo.xyz * previousAlbedoWeight + frameState.albedo * frameState.featureWeight) /
                  totalAlbedoWeight
            : float3(0.0);
    float3 accumulatedNormal =
        totalNormalWeight > 0.0
            ? (previousNormal.xyz * previousNormalWeight + frameState.normal * frameState.featureWeight) /
                  totalNormalWeight
            : float3(0.0);

    accumulationWriteImage[pixelState.pixel] = float4(accumulated, totalWeight);
    albedoWriteImage[pixelState.pixel] = float4(accumulatedAlbedo, totalAlbedoWeight);
    normalWriteImage[pixelState.pixel] = float4(accumulatedNormal, totalNormalWeight);
    outputImage[pixelState.pixel] = float4(mapAccumulatedRadiance(accumulated, mode), 1.0);
}

void writeAccumulatedFrameOutputs(
    RaygenPixelState pixelState,
    RaygenFrameState frameState,
    SpectralSinglePathMode mode
) {
    float4 previousAlbedo = albedoReadImage[pixelState.pixel];
    float4 previousNormal = normalReadImage[pixelState.pixel];
    float previousWeight = float(pixelState.previousSamples);
    float newWeight = float(pixelState.spp);
    float totalWeight = previousWeight + newWeight;
    float previousAlbedoWeight = previousAlbedo.w;
    float previousNormalWeight = previousNormal.w;
    float totalAlbedoWeight = previousAlbedoWeight + frameState.featureWeight;
    float totalNormalWeight = previousNormalWeight + frameState.featureWeight;
    float3 accumulated = (pixelState.previousAccumulation.xyz * previousWeight + frameState.radiance * newWeight) /
                         max(totalWeight, 1.0);
    float3 accumulatedAlbedo =
        totalAlbedoWeight > 0.0
            ? (previousAlbedo.xyz * previousAlbedoWeight + frameState.albedo * frameState.featureWeight) /
                  totalAlbedoWeight
            : float3(0.0);
    float3 accumulatedNormal =
        totalNormalWeight > 0.0
            ? (previousNormal.xyz * previousNormalWeight + frameState.normal * frameState.featureWeight) /
                  totalNormalWeight
            : float3(0.0);

    accumulationWriteImage[pixelState.pixel] = float4(accumulated, totalWeight);
    albedoWriteImage[pixelState.pixel] = float4(accumulatedAlbedo, totalAlbedoWeight);
    normalWriteImage[pixelState.pixel] = float4(accumulatedNormal, totalNormalWeight);
    outputImage[pixelState.pixel] = float4(mapAccumulatedRadiance(accumulated, mode), 1.0);
}

void writeAccumulatedFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState, SpectralHeroPathMode mode) {
    writeAccumulatedFrameOutputs(pixelState, frameState, SpectralSinglePathMode());
}

void writeFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState, RgbPathMode mode) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        writeDebugFrameOutputs(pixelState, frameState);
        return;
    }

    writeAccumulatedFrameOutputs(pixelState, frameState, mode);
}

void writeFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState, SpectralSinglePathMode mode) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        writeDebugFrameOutputs(pixelState, frameState);
        return;
    }

    writeAccumulatedFrameOutputs(pixelState, frameState, mode);
}

void writeFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState, SpectralHeroPathMode mode) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        writeDebugFrameOutputs(pixelState, frameState);
        return;
    }

    writeAccumulatedFrameOutputs(pixelState, frameState, mode);
}

#endif
