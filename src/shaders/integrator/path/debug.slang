#ifndef VKRT_INTEGRATOR_PATH_DEBUG_SLANG
#define VKRT_INTEGRATOR_PATH_DEBUG_SLANG

#include "../../camera/ray.slang"
#include "../../rt/payloads/scene_payload.slang"
#include "../../rt/queries/scene_query.slang"
#include "../../utility/debug.slang"
#include "./state.slang"

bool runSelectionMaskDebug(RaygenPixelState pixelState, inout RaygenFrameState frameState) {
    if (scene.debugMode != VKRT_DEBUG_MODE_SELECTION_MASK) {
        return false;
    }

    uint mask = selectionMaskImage[pixelState.pixel];
    frameState.radiance = mask != 0u ? float3(0.0, 0.6, 1.0) : float3(0.05);
    setRaygenFrameDebugEarlyOut(frameState);
    return true;
}

void captureSelectionHit(inout RaygenPixelState pixelState) {
    if (!raygenPixelCapturesSelection(pixelState)) {
        return;
    }

    SceneRayPayload pickPayload = SceneRayPayload(0u);
    traceSceneRay(pickPayload, makePrimaryRay(pixelState.pixel, float2(0.0)), 0u, 0u, 0u);
    pixelState.firstHitInstance = pickPayload.instanceIndex;
}

bool handlePrimarySurfaceDebug(
    RaygenModeState modeState,
    uint sampleIndex,
    uint depth,
    Material material,
    SurfaceTextureData textureData,
    float3 shadingNormal,
    float hitDistance,
    inout RaygenFrameState frameState
) {
    if (sampleIndex != 0u || depth != 0u) {
        return false;
    }

    if (modeState.debugMode == VKRT_DEBUG_MODE_NORMALS) {
        frameState.radiance = shadingNormal * 0.5 + 0.5;
        setRaygenFrameDebugEarlyOut(frameState);
        return true;
    }

    if (modeState.debugMode == VKRT_DEBUG_MODE_DEPTH) {
        frameState.radiance = float3(1.0 / (1.0 + hitDistance));
        setRaygenFrameDebugEarlyOut(frameState);
        return true;
    }

    if (isTextureMapDebugMode(modeState.debugMode)) {
        frameState.radiance = evaluateTextureMapDebug(material, textureData, modeState.debugMode);
        setRaygenFrameDebugEarlyOut(frameState);
        return true;
    }

    return false;
}

#endif
