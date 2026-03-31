#ifndef VKRT_INTEGRATOR_PATH_WRITEBACK_SLANG
#define VKRT_INTEGRATOR_PATH_WRITEBACK_SLANG

#include "../../camera/viewport.slang"
#include "../../film/tonemap.slang"
#include "../../utility/spectral.slang"
#include "./state.slang"

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

float3 mapAccumulatedRadiance(float3 accumulated, uint spectralOutput) {
    return spectralOutput != 0u ? mapSceneColorToDisplay(xyzToLinearSrgb(accumulated))
                                : mapSceneColorToDisplay(accumulated);
}

void writeDebugFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        accumulationWriteImage[pixelState.pixel] = float4(frameState.radiance, 0.0);
        albedoWriteImage[pixelState.pixel] = float4(0.0);
        normalWriteImage[pixelState.pixel] = float4(0.0);
        outputImage[pixelState.pixel] = float4(encodeDisplayColor(frameState.radiance), 1.0);
    }
}

float3 blendAccumulatedValue(float3 previousValue, float previousWeight, float3 currentValue, float currentWeight) {
    float totalWeight = previousWeight + currentWeight;
    if (totalWeight <= 0.0) return float3(0.0);
    return (previousValue * previousWeight + currentValue * currentWeight) / totalWeight;
}

float3 accumulatedFrameRadiance(RaygenPixelState pixelState, RaygenFrameState frameState) {
    return blendAccumulatedValue(
        pixelState.previousAccumulation.xyz,
        float(pixelState.previousSamples),
        frameState.radiance,
        float(pixelState.spp)
    );
}

void writeAccumulatedFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState, uint spectralOutput) {
    float4 previousAlbedo = albedoReadImage[pixelState.pixel];
    float4 previousNormal = normalReadImage[pixelState.pixel];
    float previousWeight = float(pixelState.previousSamples);
    float totalWeight = previousWeight + float(pixelState.spp);
    float previousAlbedoWeight = previousAlbedo.w;
    float previousNormalWeight = previousNormal.w;
    float totalAlbedoWeight = previousAlbedoWeight + frameState.featureWeight;
    float totalNormalWeight = previousNormalWeight + frameState.featureWeight;
    float3 accumulated = accumulatedFrameRadiance(pixelState, frameState);
    float3 accumulatedAlbedo =
        blendAccumulatedValue(previousAlbedo.xyz, previousAlbedoWeight, frameState.albedo, frameState.featureWeight);
    float3 accumulatedNormal =
        blendAccumulatedValue(previousNormal.xyz, previousNormalWeight, frameState.normal, frameState.featureWeight);

    accumulationWriteImage[pixelState.pixel] = float4(accumulated, totalWeight);
    albedoWriteImage[pixelState.pixel] = float4(accumulatedAlbedo, totalAlbedoWeight);
    normalWriteImage[pixelState.pixel] = float4(accumulatedNormal, totalNormalWeight);
    outputImage[pixelState.pixel] = float4(mapAccumulatedRadiance(accumulated, spectralOutput), 1.0);
}

void writeRgbAccumulatedFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState) {
    writeAccumulatedFrameOutputs(pixelState, frameState, 0u);
}

void writeSpectralSingleAccumulatedFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState) {
    writeAccumulatedFrameOutputs(pixelState, frameState, 1u);
}

void writeSpectralHeroAccumulatedFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState) {
    writeAccumulatedFrameOutputs(pixelState, frameState, 1u);
}

void writeRgbFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        writeDebugFrameOutputs(pixelState, frameState);
        return;
    }

    writeRgbAccumulatedFrameOutputs(pixelState, frameState);
}

void writeSpectralSingleFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        writeDebugFrameOutputs(pixelState, frameState);
        return;
    }

    writeSpectralSingleAccumulatedFrameOutputs(pixelState, frameState);
}

void writeSpectralHeroFrameOutputs(RaygenPixelState pixelState, RaygenFrameState frameState) {
    if (raygenFrameDebugEarlyOut(frameState)) {
        writeDebugFrameOutputs(pixelState, frameState);
        return;
    }

    writeSpectralHeroAccumulatedFrameOutputs(pixelState, frameState);
}

#endif
