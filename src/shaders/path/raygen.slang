#include "scene/resources.slang"
#include "bsdf/bsdf.slang"
#include "light/direct.slang"
#include "sample/random.slang"
#include "camera/ray.slang"
#include "geometry/packing.slang"
#include "film/tonemap.slang"
#include "camera/viewport.slang"
#include "trace/scene.slang"
#include "payload.slang"

[shader("raygeneration")]
void main() {
    static const uint kSceneSerHintBits = 2u;

    int2 pixel = int2(DispatchRaysIndex().xy);
    if (!insideViewport(pixel)) {
        accumulationWriteImage[pixel] = float4(0.0);
        outputImage[pixel] = float4(0.0);
        return;
    }

    uint spp = max(scene.samplesPerPixel, 1u);
    float4 previous = accumulationReadImage[pixel];
    uint previousSamples = (uint)(previous.w + 0.5);

    int2 selectionPixel = int2(int(selection[0].pixel & 0xffffu), int((selection[0].pixel >> 16) & 0xffffu));
    bool captureSelection = all(pixel == selectionPixel);
    uint firstHitInstance = VKRT_INVALID_INDEX;

    uint debugMode = scene.debugMode;
    bool bsdfOnlyDebug = debugMode == VKRT_DEBUG_MODE_BSDF_ONLY;
    bool neeOnlyDebug = debugMode == VKRT_DEBUG_MODE_NEE_ONLY;
    bool bounceCountDebug = debugMode == VKRT_DEBUG_MODE_BOUNCE_COUNT;
    bool neeEnabled = scene.misNeeEnabled != 0u && scene.emissiveMeshCount > 0u;

    float3 frameRadiance = float3(0.0);
    bool debugEarlyOut = false;

    if (debugMode == VKRT_DEBUG_MODE_SELECTION_MASK) {
        uint mask = selectionMaskImage[pixel];
        frameRadiance = mask != 0u ? float3(0.0, 0.6, 1.0) : float3(0.05);
        debugEarlyOut = true;

        if (captureSelection) {
            Payload pickPayload = Payload.make();
            traceSceneRay(pickPayload, makePrimaryRay(pixel, float2(0.0)), 0u, 0u, 0u);
            firstHitInstance = pickPayload.instanceIndex;
        }
    }

    for (uint sampleIndex = 0u; sampleIndex < spp && !debugEarlyOut; sampleIndex++) {
        float3 radiance = float3(0.0);
        float3 throughput = float3(1.0);
        MediumState medium;
        medium.refractiveActive = false;
        medium.absorptionActive = false;
        medium.absorptionSigma = float3(0.0);
        bool prevVertexNeeAllowed = false;

        uint rng = initPixelSeed(pixel, scene.frameNumber, previousSamples + sampleIndex);
        float2 jitter = float2(rand(rng), rand(rng)) - float2(0.5);
        RayDesc ray = makePrimaryRay(pixel, jitter);

        float prevBsdfPdf = 0.0;
        uint bounceCount = 0u;

        for (uint depth = 0u; depth < scene.rrMaxDepth; depth++) {
            Payload payload = Payload.make();
            uint sceneRayHint = (medium.refractiveActive ? 1u : 0u) |
                (medium.absorptionActive ? 2u : 0u);
            traceSceneRay(payload, ray, depth, sceneRayHint, kSceneSerHintBits);

            if (sampleIndex == 0u && depth == 0u) {
                firstHitInstance = payload.instanceIndex;

                if (payload.didHit != 0u) {
                    if (debugMode == VKRT_DEBUG_MODE_NORMALS) {
                        float3 shadingNormal = unpackOctNormal(payload.packedShadingNormal);
                        frameRadiance = shadingNormal * 0.5 + 0.5;
                        debugEarlyOut = true;
                        break;
                    }

                    if (debugMode == VKRT_DEBUG_MODE_DEPTH) {
                        frameRadiance = float3(1.0 / (1.0 + payload.hitDistance));
                        debugEarlyOut = true;
                        break;
                    }
                }
            }

            if (payload.didHit == 0u) {
                if (!neeOnlyDebug && !mediumHasActiveBoundary(medium)) {
                    radiance += throughput * scene.environmentLight.xyz;
                }
                break;
            }

            float3 hitPoint = ray.Origin + ray.Direction * payload.hitDistance;
            float3 shadingNormal = unpackOctNormal(payload.packedShadingNormal);
            float3 geometricNormal = unpackOctNormal(payload.packedGeometricNormal);
            Material material = materials[payload.materialIndex];
            if (medium.absorptionActive) {
                throughput *= mediumTransmittance(medium, payload.hitDistance);
                if (!any(throughput > 0.0)) {
                    break;
                }
            }
            ShadingBasis basis = makeShadingBasis(shadingNormal, payload.tangent);
            float3 wo = worldToLocal(-ray.Direction, basis);
            BSDFState state = makeBSDFState(material, wo, payload.frontFace);
            bool currentVertexNeeAllowed = !medium.refractiveActive;
            bool vertexNeeSupported = currentVertexNeeAllowed;

            float3 contribution = float3(0.0);
            if (!bsdfOnlyDebug && neeEnabled && currentVertexNeeAllowed) {
                DirectLightResult directLight = sampleDirectLight(
                    hitPoint,
                    geometricNormal,
                    basis,
                    state,
                    medium,
                    rng
                );
                contribution += directLight.radiance;
                vertexNeeSupported = directLight.neeSupported;
            }

            if (!neeOnlyDebug) {
                float3 emission = material.emissionColor * material.emissionLuminance;
                if (any(emission > 0.0)) {
                    float misWeight = 1.0;
                    if (neeEnabled && prevVertexNeeAllowed && depth > 0u && prevBsdfPdf > 0.0) {
                        misWeight = computeBSDFEmitterMISWeight(
                            prevBsdfPdf,
                            meshInfos[payload.instanceIndex].lightPdfArea,
                            geometricNormal,
                            ray.Direction,
                            payload.hitDistance
                        );
                    }
                    contribution += emission * misWeight;
                }
            }

            radiance += throughput * contribution;
            bounceCount = depth + 1u;

            BSDFSample sample = sampleBSDF(state, basis, rng);
            if (!sample.isUsable()) {
                break;
            }

            throughput *= sample.weight;
            prevBsdfPdf = sample.pdf;
            if (!any(throughput > 0.0)) {
                break;
            }

            bool sampledPathNeeAllowed = vertexNeeSupported;
            if (sampledPathNeeAllowed && sample.isTransmission != 0u) {
                sampledPathNeeAllowed = false;
            }

            updateMediumStateFromTransmission(
                material,
                payload.frontFace,
                sample.isTransmission,
                medium
            );
            prevVertexNeeAllowed = sampledPathNeeAllowed;

            if (depth + 1u >= scene.rrMinDepth) {
                float continueProbability = clamp(max(throughput.x, max(throughput.y, throughput.z)), VKRT_RR_MIN_CONTINUE_PROB, VKRT_RR_MAX_CONTINUE_PROB);
                if (rand(rng) > continueProbability) {
                    break;
                }
                throughput /= continueProbability;
            }

            float3 offsetDir = sample.isTransmission != 0u ? -geometricNormal : geometricNormal;
            ray.Origin = hitPoint + offsetDir * VKRT_SHADOW_ORIGIN_OFFSET;
            ray.Direction = sample.wi;
            ray.TMin = VKRT_RAY_T_MIN;
            ray.TMax = VKRT_RAY_T_MAX;
        }

        if (bounceCountDebug && sampleIndex == 0u) {
            float t = clamp(float(bounceCount) / max(float(scene.rrMaxDepth), 1.0), 0.0, 1.0);
            frameRadiance = float3(t, 1.0 - t, 0.0);
            debugEarlyOut = true;
        }

        if (!debugEarlyOut) {
            frameRadiance += radiance;
        }
    }

    if (!debugEarlyOut) {
        frameRadiance /= float(spp);
    }

    if (captureSelection) {
        selection[0].hitMeshIndex = firstHitInstance;
    }

    if (debugEarlyOut) {
        accumulationWriteImage[pixel] = float4(frameRadiance, 0.0);
        outputImage[pixel] = float4(frameRadiance, 1.0);
    } else {
        float previousWeight = float(previousSamples);
        float newWeight = float(spp);
        float totalWeight = previousWeight + newWeight;
        float3 accumulated = (previous.xyz * previousWeight + frameRadiance * newWeight) / max(totalWeight, 1.0);
        float3 mapped = toneMap(accumulated);

        accumulationWriteImage[pixel] = float4(accumulated, totalWeight);
        outputImage[pixel] = float4(mapped, 1.0);
    }
}
