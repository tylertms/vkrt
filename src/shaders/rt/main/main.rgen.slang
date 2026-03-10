#include "../../core/resources.slang"
#include "../../utility/random.slang"
#include "../../utility/ray.slang"
#include "../../utility/tonemap.slang"
#include "../../utility/viewport.slang"

[shader("raygeneration")]
void main() {
    int2 pixel = int2(DispatchRaysIndex().xy);
    if (!insideViewport(pixel)) {
        accumulationWriteImage[pixel] = float4(0.0);
        outputImage[pixel] = float4(0.0);
        return;
    }

    uint spp = max(scene.samplesPerPixel, 1u);
    float4 previous = accumulationReadImage[pixel];
    uint previousSamples = (uint)(previous.w + 0.5);

    int2 requestPixel = int2(int(pickBuffer[0].pixel & 0xffffu), int((pickBuffer[0].pixel >> 16) & 0xffffu));
    bool capturePick = all(pixel == requestPixel);
    uint firstHitInstance = VKRT_INVALID_INDEX;

    float3 frameRadiance = float3(0.0);
    for (uint sampleIndex = 0; sampleIndex < spp; ++sampleIndex) {
        uint rngState = initPixelSeed(pixel, scene.frameNumber, previousSamples + sampleIndex);
        float2 jitter = float2(randomFloat(rngState), randomFloat(rngState)) - float2(0.5);

        Payload payload;
        payload.radiance = float3(0.0);
        payload.normal = float3(0.0, 1.0, 0.0);
        payload.didHit = 0u;
        payload.instanceIndex = VKRT_INVALID_INDEX;
        payload.hitDistance = 0.0;

        RayDesc ray = makePrimaryRay(pixel, jitter);
        TraceRay(topLevelAS, 0u, 0xffu, 0u, 0u, 0u, ray, payload);
        frameRadiance += payload.radiance;

        if (sampleIndex == 0u) {
            firstHitInstance = payload.instanceIndex;
        }
    }

    if (capturePick) {
        pickBuffer[0].hitMeshIndex = firstHitInstance;
    }

    frameRadiance /= float(spp);

    float previousWeight = float(previousSamples);
    float newWeight = float(spp);
    float totalWeight = previousWeight + newWeight;
    float3 accumulated = (previous.xyz * previousWeight + frameRadiance * newWeight) / max(totalWeight, 1.0);
    float3 mapped = scene.toneMappingMode == VKRT_TONE_MAPPING_MODE_ACES ? toneMapACES(accumulated) : accumulated;

    accumulationWriteImage[pixel] = float4(accumulated, totalWeight);
    outputImage[pixel] = float4(mapped, 1.0);
}
