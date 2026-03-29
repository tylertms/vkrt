#include "../bsdf/bsdf.slang"
#include "../camera/ray.slang"
#include "../camera/viewport.slang"
#include "../film/tonemap.slang"
#include "../geometry/packing.slang"
#include "../light/direct.slang"
#include "../light/environment.slang"
#include "../material/textures.slang"
#include "../sample/random.slang"
#include "../sample/wavelength.slang"
#include "../scene/resources.slang"
#include "../trace/scene.slang"
#include "../utility/debug.slang"
#include "payload.slang"

[shader("raygeneration")] void main() {
    static const uint kSceneSerHintBits = 2u;

    int2 pixel = int2(DispatchRaysIndex().xy);
    if (!insideViewport(pixel)) {
        accumulationWriteImage[pixel] = float4(0.0);
        albedoWriteImage[pixel] = float4(0.0);
        normalWriteImage[pixel] = float4(0.0);
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
    bool denoiserAlbedoDebug = debugMode == VKRT_DEBUG_MODE_DENOISER_ALBEDO;
    bool denoiserNormalDebug = debugMode == VKRT_DEBUG_MODE_DENOISER_NORMAL;
    bool denoiserFeatureValidityDebug = debugMode == VKRT_DEBUG_MODE_DENOISER_FEATURE_VALIDITY;
    bool denoiserFeatureDepthDebug = debugMode == VKRT_DEBUG_MODE_DENOISER_FEATURE_DEPTH;
    bool denoiserFollowSpecularDebug = debugMode == VKRT_DEBUG_MODE_DENOISER_FOLLOW_SPECULAR;
    bool neeEnabled = scene.misNeeEnabled != 0u && scene.emissiveMeshCount > 0u;
    bool spectralMode = spectralRenderingEnabled();
    bool heroWavelengthMode = heroWavelengthSamplingEnabled();
    bool singleWavelengthSpectralMode = spectralMode && !heroWavelengthMode;

    float3 frameRadiance = float3(0.0);
    float3 frameAlbedo = float3(0.0);
    float3 frameNormal = float3(0.0);
    float frameFeatureWeight = 0.0;
    float frameFeatureDepth = 0.0;
    float frameFollowSpecular = 0.0;
    bool debugEarlyOut = false;

    if (debugMode == VKRT_DEBUG_MODE_SELECTION_MASK) {
        uint mask = selectionMaskImage[pixel];
        frameRadiance = mask != 0u ? float3(0.0, 0.6, 1.0) : float3(0.05);
        debugEarlyOut = true;

        if (captureSelection) {
            Payload pickPayload = Payload.make(0u);
            traceSceneRay(pickPayload, makePrimaryRay(pixel, float2(0.0)), 0u, 0u, 0u);
            firstHitInstance = pickPayload.instanceIndex;
        }
    }

    for (uint sampleIndex = 0u; sampleIndex < spp && !debugEarlyOut; sampleIndex++) {
        float3 radiance = float3(0.0);
        float spectralRadianceScalar = 0.0;
        float4 spectralRadiance = float4(0.0, 0.0, 0.0, 0.0);
        float3 sampleAlbedo = float3(0.0);
        float3 sampleNormal = float3(0.0);
        float sampleFeatureWeight = 0.0;
        float sampleFeatureDepth = 0.0;
        float sampleFollowSpecular = 0.0;
        bool sampleFeaturesResolved = false;

        uint rng = initPixelSeed(pixel, scene.frameNumber, previousSamples + sampleIndex);
        WavelengthSample wavelength = {};
        float4 wavelengthsNm = float4(0.0, 0.0, 0.0, 0.0);
        float4 invWavelengthPdf = float4(0.0, 0.0, 0.0, 0.0);
        float heroLambdaNm = 0.0;
        if (singleWavelengthSpectralMode) {
            wavelength = sampleUniformWavelength(rng, previousSamples + sampleIndex);
            heroLambdaNm = wavelength.lambdaNm;
        } else if (spectralMode) {
            wavelengthsNm = sampleHeroWavelengths4(rng, previousSamples + sampleIndex);
            invWavelengthPdf = heroWavelengthInvPdf4();
            heroLambdaNm = wavelengthsNm.x;
        }
        float3 throughput = float3(1.0);
        float spectralThroughputScalar = 1.0;
        float4 spectralThroughput = heroWavelengthMode ? float4(1.0, 1.0, 1.0, 1.0) : float4(1.0, 0.0, 0.0, 0.0);

        MediumState medium;
        medium.refractiveActive = false;
        medium.absorptionActive = false;
        medium.absorptionSigma = float3(0.0);
        medium.spectralAbsorptionSigma = float4(0.0);
        bool prevVertexNeeAllowed = false;
        float4 techniquePathPdf = heroWavelengthMode ? float4(1.0) : float4(1.0, 0.0, 0.0, 0.0);
        float4 prevVertexTechniquePathPdf = float4(0.0);
        float4 prevBsdfTechniquePdf = float4(0.0);

        float2 jitter = float2(rand(rng), rand(rng)) - float2(0.5);
        RayDesc ray = makePrimaryRay(pixel, jitter);

        float prevBsdfPdf = 0.0;
        uint bounceCount = 0u;

        for (uint depth = 0u; depth < scene.rrMaxDepth; depth++) {
            Payload payload = Payload.make(rng);
            uint sceneRayHint = (medium.refractiveActive ? 1u : 0u) | (medium.absorptionActive ? 2u : 0u);
            traceSceneRay(payload, ray, depth, sceneRayHint, kSceneSerHintBits);
            rng = payload.rng;

            if (sampleIndex == 0u && depth == 0u) {
                firstHitInstance = payload.instanceIndex;
            }

            if (payload.didHit == 0u) {
                if (!neeOnlyDebug && !mediumHasActiveBoundary(medium)) {
                    if (heroWavelengthMode) {
                        spectralRadiance +=
                            spectralThroughput * heroWavelengthBalanceWeight(techniquePathPdf) *
                            spectralScalarFromLinearSrgb4(sampleEnvironmentRadiance(ray.Direction), wavelengthsNm);
                    } else if (singleWavelengthSpectralMode) {
                        spectralRadianceScalar +=
                            spectralThroughputScalar *
                            spectralScalarFromLinearSrgb(sampleEnvironmentRadiance(ray.Direction), wavelength.lambdaNm);
                    } else {
                        radiance += throughput * sampleEnvironmentRadiance(ray.Direction);
                    }
                }
                break;
            }

            if (medium.absorptionActive) {
                if (heroWavelengthMode) {
                    spectralThroughput *= mediumSpectralTransmittance(medium, payload.hitDistance);
                    if (!any(spectralThroughput > 0.0)) {
                        break;
                    }
                } else if (singleWavelengthSpectralMode) {
                    spectralThroughputScalar *= mediumTransmittance(medium, payload.hitDistance).x;
                    if (spectralThroughputScalar <= 0.0) {
                        break;
                    }
                } else {
                    float3 transmittance = mediumTransmittance(medium, payload.hitDistance);
                    throughput *= transmittance;
                    if (!any(throughput > 0.0)) {
                        break;
                    }
                }
            }
            if (!spectralMode && !any(throughput > 0.0)) {
                break;
            }
            if (heroWavelengthMode && !any(spectralThroughput > 0.0)) {
                break;
            }
            if (singleWavelengthSpectralMode && spectralThroughputScalar <= 0.0) {
                break;
            }
            float3 hitPoint = ray.Origin + ray.Direction * payload.hitDistance;
            float3 shadingNormal = unpackOctNormal(payload.packedShadingNormal);
            float3 geometricNormal = unpackOctNormal(payload.packedGeometricNormal);
            Material material = materials[payload.materialIndex];
            ShadingBasis basis = makeShadingBasis(shadingNormal, payload.tangent);
            shadingNormal = applyNormalTexture(material, payload.texcoord0, payload.texcoord1, basis);
            shadingNormal = sanitizeShadingNormal(shadingNormal, geometricNormal, -ray.Direction);
            basis = makeShadingBasis(shadingNormal, payload.tangent);

            if (sampleIndex == 0u && depth == 0u) {
                if (debugMode == VKRT_DEBUG_MODE_NORMALS) {
                    frameRadiance = shadingNormal * 0.5 + 0.5;
                    debugEarlyOut = true;
                    break;
                }

                if (debugMode == VKRT_DEBUG_MODE_DEPTH) {
                    frameRadiance = float3(1.0 / (1.0 + payload.hitDistance));
                    debugEarlyOut = true;
                    break;
                }

                if (isTextureMapDebugMode(debugMode)) {
                    frameRadiance = evaluateTextureMapDebug(material, payload.texcoord0, payload.texcoord1, debugMode);
                    debugEarlyOut = true;
                    break;
                }
            }

            applySurfaceTextures(material, payload.color, payload.texcoord0, payload.texcoord1);
            bool shouldFollowSpecularHit = materialDenoiserShouldFollowSpecularHit(material, payload.frontFace);
            if (depth == 0u) {
                sampleFollowSpecular = shouldFollowSpecularHit ? 1.0 : 0.0;
            }
            if (!sampleFeaturesResolved && !shouldFollowSpecularHit) {
                sampleAlbedo = materialDenoiserAlbedo(material);
                sampleNormal = shadingNormal;
                sampleFeatureWeight = 1.0;
                sampleFeatureDepth = float(depth + 1u);
                sampleFeaturesResolved = true;
            }

            float3 wo = worldToLocal(-ray.Direction, basis);
            BSDFState state = makeBSDFState(material, wo, payload.frontFace, heroLambdaNm, spectralMode ? 1u : 0u);
            bool currentVertexNeeAllowed = !medium.refractiveActive;
            bool vertexNeeSupported = currentVertexNeeAllowed;

            float3 contribution = float3(0.0);
            float spectralContributionScalar = 0.0;
            float4 spectralContribution = float4(0.0, 0.0, 0.0, 0.0);
            if (!bsdfOnlyDebug && neeEnabled && currentVertexNeeAllowed) {
                DirectLightResult directLight = sampleDirectLight(
                    hitPoint,
                    geometricNormal,
                    basis,
                    state,
                    medium,
                    techniquePathPdf,
                    wavelengthsNm,
                    rng
                );
                if (heroWavelengthMode) {
                    spectralContribution += directLight.spectralRadiance;
                } else if (singleWavelengthSpectralMode) {
                    spectralContributionScalar += directLight.spectralRadiance.x;
                } else {
                    contribution += directLight.radiance;
                }
                vertexNeeSupported = directLight.neeSupported;
            }

            if (!neeOnlyDebug) {
                float3 emission = material.emissionColor * material.emissionLuminance;
                if (any(emission > 0.0)) {
                    float misWeight = 1.0;
                    if (heroWavelengthMode) {
                        misWeight = heroWavelengthBalanceWeight(techniquePathPdf);
                        if (neeEnabled && prevVertexNeeAllowed && depth > 0u) {
                            float lightPdfSolidAngle = lightPdfAreaToSolidAngle(
                                meshInfos[payload.instanceIndex].lightPdfArea,
                                geometricNormal,
                                ray.Direction,
                                payload.hitDistance
                            );
                            misWeight = computeSpectralEmitterMISWeight(
                                prevVertexTechniquePathPdf,
                                prevBsdfTechniquePdf,
                                lightPdfSolidAngle
                            );
                        }
                    } else if (singleWavelengthSpectralMode) {
                        if (neeEnabled && prevVertexNeeAllowed && depth > 0u && prevBsdfPdf > 0.0) {
                            misWeight = computeBSDFEmitterMISWeight(
                                prevBsdfPdf,
                                meshInfos[payload.instanceIndex].lightPdfArea,
                                geometricNormal,
                                ray.Direction,
                                payload.hitDistance
                            );
                        }
                    } else if (neeEnabled && prevVertexNeeAllowed && depth > 0u && prevBsdfPdf > 0.0) {
                        misWeight = computeBSDFEmitterMISWeight(
                            prevBsdfPdf,
                            meshInfos[payload.instanceIndex].lightPdfArea,
                            geometricNormal,
                            ray.Direction,
                            payload.hitDistance
                        );
                    }
                    if (heroWavelengthMode) {
                        spectralContribution += spectralScalarFromLinearSrgb4(emission, wavelengthsNm) * misWeight;
                    } else if (singleWavelengthSpectralMode) {
                        spectralContributionScalar +=
                            spectralScalarFromLinearSrgb(emission, wavelength.lambdaNm) * misWeight;
                    } else {
                        contribution += emission * misWeight;
                    }
                }
            }

            if (heroWavelengthMode) {
                spectralRadiance += spectralThroughput * spectralContribution;
            } else if (singleWavelengthSpectralMode) {
                spectralRadianceScalar += spectralThroughputScalar * spectralContributionScalar;
            } else {
                radiance += throughput * contribution;
            }
            bounceCount = depth + 1u;

            uint sampleIsTransmission = 0u;
            float3 sampleWi = float3(0.0);
            if (heroWavelengthMode) {
                SpectralBSDFSample sample = sampleSpectralBSDF(state, basis, wavelengthsNm, rng);
                if (!sample.isUsable()) {
                    break;
                }

                prevVertexTechniquePathPdf = techniquePathPdf;
                prevBsdfTechniquePdf = sample.techniquePdf;
                techniquePathPdf *= sample.techniquePdf;
                spectralThroughput *= sample.weight;
                if (!any(spectralThroughput > 0.0)) {
                    break;
                }

                sampleIsTransmission = sample.isTransmission;
                sampleWi = sample.wi;
            } else if (singleWavelengthSpectralMode) {
                BSDFSample sample = sampleBSDF(state, basis, rng);
                if (!sample.isUsable()) {
                    break;
                }

                spectralThroughputScalar *= sample.weight.x;
                if (spectralThroughputScalar <= 0.0) {
                    break;
                }

                prevBsdfPdf = sample.pdf;
                sampleIsTransmission = sample.isTransmission;
                sampleWi = sample.wi;
            } else {
                BSDFSample sample = sampleBSDF(state, basis, rng);
                if (!sample.isUsable()) {
                    break;
                }

                throughput *= sample.weight;
                if (!any(throughput > 0.0)) {
                    break;
                }

                prevBsdfPdf = sample.pdf;
                sampleIsTransmission = sample.isTransmission;
                sampleWi = sample.wi;
            }

            bool sampledPathNeeAllowed = vertexNeeSupported;
            if (sampledPathNeeAllowed && sampleIsTransmission != 0u) {
                sampledPathNeeAllowed = false;
            }

            if (heroWavelengthMode) {
                updateMediumStateFromTransmissionSpectral(
                    material,
                    payload.frontFace,
                    sampleIsTransmission,
                    wavelengthsNm,
                    medium
                );
            } else if (singleWavelengthSpectralMode) {
                updateMediumStateFromTransmission(
                    material,
                    payload.frontFace,
                    sampleIsTransmission,
                    wavelength.lambdaNm,
                    1u,
                    medium
                );
            } else {
                updateMediumStateFromTransmission(
                    material,
                    payload.frontFace,
                    sampleIsTransmission,
                    heroLambdaNm,
                    0u,
                    medium
                );
            }
            prevVertexNeeAllowed = sampledPathNeeAllowed;

            if (depth + 1u >= scene.rrMinDepth) {
                float continueProbability =
                    heroWavelengthMode
                        ? clamp(maxComponent4(spectralThroughput), VKRT_RR_MIN_CONTINUE_PROB, VKRT_RR_MAX_CONTINUE_PROB)
                    : singleWavelengthSpectralMode
                        ? clamp(spectralThroughputScalar, VKRT_RR_MIN_CONTINUE_PROB, VKRT_RR_MAX_CONTINUE_PROB)
                        : clamp(maxComponent(throughput), VKRT_RR_MIN_CONTINUE_PROB, VKRT_RR_MAX_CONTINUE_PROB);
                if (rand(rng) > continueProbability) {
                    break;
                }
                if (heroWavelengthMode) {
                    techniquePathPdf *= continueProbability.xxxx;
                    spectralThroughput /= continueProbability;
                } else if (singleWavelengthSpectralMode) {
                    spectralThroughputScalar /= continueProbability;
                } else {
                    throughput /= continueProbability;
                }
            }

            float3 offsetDir = sampleIsTransmission != 0u ? -geometricNormal : geometricNormal;
            ray.Origin = hitPoint + offsetDir * VKRT_SHADOW_ORIGIN_OFFSET;
            ray.Direction = sampleWi;
            ray.TMin = VKRT_RAY_T_MIN;
            ray.TMax = VKRT_RAY_T_MAX;
        }

        if (bounceCountDebug && sampleIndex == 0u) {
            float t = clamp(float(bounceCount) / max(float(scene.rrMaxDepth), 1.0), 0.0, 1.0);
            frameRadiance = float3(t, 1.0 - t, 0.0);
            debugEarlyOut = true;
        }

        if (!debugEarlyOut) {
            frameRadiance +=
                heroWavelengthMode ? spectralSample4ToLinearSrgb(spectralRadiance, wavelengthsNm, invWavelengthPdf)
                : singleWavelengthSpectralMode ? spectralSampleToLinearSrgb(spectralRadianceScalar, wavelength)
                                               : radiance;
            frameAlbedo += sampleAlbedo;
            frameNormal += sampleNormal;
            frameFeatureWeight += sampleFeatureWeight;
            frameFeatureDepth += sampleFeatureDepth;
            frameFollowSpecular += sampleFollowSpecular;
        }
    }

    if (!debugEarlyOut) {
        frameRadiance /= float(spp);
        if (frameFeatureWeight > 0.0) {
            frameAlbedo /= frameFeatureWeight;
            frameNormal /= frameFeatureWeight;
        } else {
            frameAlbedo = float3(0.0);
            frameNormal = float3(0.0);
        }
    }

    if (!debugEarlyOut) {
        if (denoiserAlbedoDebug) {
            frameRadiance = frameAlbedo;
            debugEarlyOut = true;
        } else if (denoiserNormalDebug) {
            frameRadiance = frameFeatureWeight > 0.0 ? frameNormal * 0.5 + 0.5 : float3(0.0);
            debugEarlyOut = true;
        } else if (denoiserFeatureValidityDebug) {
            float coverage = saturate(frameFeatureWeight / float(spp));
            frameRadiance = coverage.xxx;
            debugEarlyOut = true;
        } else if (denoiserFeatureDepthDebug) {
            float normalizedDepth = 0.0;
            if (frameFeatureWeight > 0.0) {
                float averageFeatureDepth = frameFeatureDepth / frameFeatureWeight;
                normalizedDepth = saturate((averageFeatureDepth - 1.0) / max(float(scene.rrMaxDepth - 1u), 1.0));
            }
            frameRadiance = normalizedDepth.xxx;
            debugEarlyOut = true;
        } else if (denoiserFollowSpecularDebug) {
            float followRatio = saturate(frameFollowSpecular / float(spp));
            frameRadiance = lerp(float3(0.05), float3(1.0, 0.6, 0.0), followRatio);
            debugEarlyOut = true;
        }
    }

    if (captureSelection) {
        selection[0].hitMeshIndex = firstHitInstance;
    }

    if (debugEarlyOut) {
        accumulationWriteImage[pixel] = float4(frameRadiance, 0.0);
        albedoWriteImage[pixel] = float4(0.0);
        normalWriteImage[pixel] = float4(0.0);
        outputImage[pixel] = float4(encodeDisplayColor(frameRadiance), 1.0);
    } else {
        float4 previousAlbedo = albedoReadImage[pixel];
        float4 previousNormal = normalReadImage[pixel];
        float previousWeight = float(previousSamples);
        float newWeight = float(spp);
        float totalWeight = previousWeight + newWeight;
        float previousAlbedoWeight = previousAlbedo.w;
        float previousNormalWeight = previousNormal.w;
        float totalAlbedoWeight = previousAlbedoWeight + frameFeatureWeight;
        float totalNormalWeight = previousNormalWeight + frameFeatureWeight;
        float3 accumulated = (previous.xyz * previousWeight + frameRadiance * newWeight) / max(totalWeight, 1.0);
        float3 accumulatedAlbedo =
            totalAlbedoWeight > 0.0
                ? (previousAlbedo.xyz * previousAlbedoWeight + frameAlbedo * frameFeatureWeight) / totalAlbedoWeight
                : float3(0.0);
        float3 accumulatedNormal =
            totalNormalWeight > 0.0
                ? (previousNormal.xyz * previousNormalWeight + frameNormal * frameFeatureWeight) / totalNormalWeight
                : float3(0.0);
        float3 mapped = mapSceneColorToDisplay(accumulated);

        accumulationWriteImage[pixel] = float4(accumulated, totalWeight);
        albedoWriteImage[pixel] = float4(accumulatedAlbedo, totalAlbedoWeight);
        normalWriteImage[pixel] = float4(accumulatedNormal, totalNormalWeight);
        outputImage[pixel] = float4(mapped, 1.0);
    }
}
