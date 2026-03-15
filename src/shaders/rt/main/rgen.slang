#include "../../core/resources.slang"
#include "../../utility/random.slang"
#include "../../utility/ray.slang"
#include "../../utility/tonemap.slang"
#include "../../utility/viewport.slang"
#include "../../trace/scene.slang"
#include "payload.slang"

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

    for (uint sampleIndex = 0u; sampleIndex < spp; sampleIndex++) {
        float3 radiance = float3(0.0);
        float3 throughput = float3(1.0);

        uint rng = initPixelSeed(pixel, scene.frameNumber, previousSamples + sampleIndex);
        float2 jitter = float2(rand(rng), rand(rng)) - float2(0.5);
        RayDesc ray = makePrimaryRay(pixel, jitter);

        for (uint depth = 0u; depth < scene.rrMaxDepth; depth++) {
            Payload payload = Payload.make(rng);
            traceSceneRay(payload, ray, depth);
            rng = payload.rngState;

            if (sampleIndex == 0u && depth == 0u) {
                firstHitInstance = payload.instanceIndex;
            }

            radiance += throughput * payload.contribution;

            if (payload.didScatter == 0u) {
                break;
            }

            throughput *= payload.bsdfWeight;
            if (!any(throughput > 0.0)) {
                break;
            }

            if (depth + 1u >= scene.rrMinDepth) {
                float continueProbability = clamp(max(throughput.x, max(throughput.y, throughput.z)), 0.05, 0.95);
                if (rand(rng) > continueProbability) {
                    break;
                }
                throughput /= continueProbability;
            }

            ray.Origin = payload.nextOrigin;
            ray.Direction = payload.nextDirection;
            ray.TMin = 0.001;
            ray.TMax = 10000.0;
        }

        frameRadiance += radiance;
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
