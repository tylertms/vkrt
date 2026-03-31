#include "../../integrator/path/rgb/integrator.slang"
#include "../../integrator/path/writeback.slang"

[shader("raygeneration")] void main() {
    int2 pixel = int2(DispatchRaysIndex().xy);
    if (!insideViewport(pixel)) {
        accumulationWriteImage[pixel] = float4(0.0);
        albedoWriteImage[pixel] = float4(0.0);
        normalWriteImage[pixel] = float4(0.0);
        outputImage[pixel] = float4(0.0);
        return;
    }

    RaygenModeState modeState = RaygenModeState();
    RaygenPixelState pixelState = RaygenPixelState(pixel);
    RaygenFrameState frameState = RaygenFrameState();

    if (runSelectionMaskDebug(pixelState, frameState)) {
        captureSelectionHit(pixelState);
    } else {
        for (uint sampleIndex = 0u; sampleIndex < pixelState.spp && !raygenFrameDebugEarlyOut(frameState);
             sampleIndex++) {
            traceRgbPathSample(pixelState, modeState, sampleIndex, frameState);
        }
    }

    finalizeFrameAccumulation(pixelState, frameState);
    applyFrameDebugOverrides(pixelState, modeState, frameState);

    if (raygenPixelCapturesSelection(pixelState)) {
        selection[0].hitMeshIndex = pixelState.firstHitInstance;
    }

    writeRgbFrameOutputs(pixelState, frameState);
}
