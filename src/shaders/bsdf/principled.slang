#ifndef VKRT_BSDF_MODEL_SLANG
#define VKRT_BSDF_MODEL_SLANG

#include "base.slang"
#include "lobes/clearcoat.slang"
#include "lobes/dielectric.slang"
#include "lobes/ggx.slang"
#include "lobes/lambertian.slang"
#include "lobes/oren_nayar.slang"
#include "lobes/sheen.slang"
#include "lobes/subsurface.slang"

// Layered Principled BSDF:
// Sheen on top, then clearcoat, then the base dielectric/metal/substrate stack.
//
// References:
// Blender Principled node definition - https://github.com/blender/blender/blob/main/source/blender/nodes/shader/nodes/node_shader_bsdf_principled.cc
// Blender Cycles closure layering - https://github.com/blender/blender/blob/main/intern/cycles/kernel/svm/closure.h

struct BSDFDirectionSample {
    float3 wi;
    uint isTransmission;
};

struct BSDFBranchWeights {
    float sheen;
    float coat;
    float metal;
    float dielectric;
    float diffuse;
    float subsurface;
};

struct BSDFState {
    BSDFMaterial material;
    float3 wo;
    GGXParams ggx;
    ClearcoatParams clearcoat;
    uint frontFace;
    float wavelengthNm;
    uint spectralMode;
    BSDFBranchWeights sampleWeights;

    __init(BSDFMaterial material, float3 wo, uint frontFace, float wavelengthNm, uint spectralMode) {
        this.material = material;
        this.wo = wo;
        this.frontFace = frontFace;
        this.wavelengthNm = wavelengthNm;
        this.spectralMode = spectralMode;
        ggx = makeGGXParams(material, wo);
        clearcoat = makeClearcoatParams(material);
        sampleWeights = makeBSDFBranchWeights(material, wo, frontFace);
    }
};

bool useInteriorDielectricInterface(BSDFState state) {
    return state.material.transmission > 0.0 && state.frontFace == 0u;
}

float scalarSchlickFresnel(float cosThetaValue, float f0) {
    return lerp(f0, 1.0, schlickWeight(abs(cosThetaValue)));
}

float coatDirectionalAttenuation(BSDFMaterial material, float3 w) {
    if (material.clearcoat <= 0.0) {
        return 1.0;
    }

    if (absCosTheta(w) <= 0.0) {
        return 0.0;
    }

    float coatFresnel = 0.25 * material.clearcoat * scalarSchlickFresnel(absCosTheta(w), 0.04);
    return 1.0 - saturate(coatFresnel);
}

float sheenDirectionalAttenuation(float3 sheenColor, float3 w, float sheenRoughness) {
    float sheenWeight = maxComponent(sheenColor);
    return sheenLayerAttenuation(sheenWeight, cosTheta(w), sheenRoughness);
}

float2 dielectricDirectionalAlbedoAB(float roughness, float cosThetaValue) {
    float clampedRoughness = saturate(roughness);
    float clampedCosTheta = saturate(cosThetaValue);
    float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = clampedRoughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * clampedCosTheta)) * r.x + r.y;
    return saturate(float2(-1.04, 1.04) * a004 + r.zw);
}

float3 dielectricDirectionalAlbedo(BSDFMaterial material, float3 w) {
    if (absCosTheta(w) <= 0.0) {
        return float3(1.0);
    }

    float2 ab = dielectricDirectionalAlbedoAB(material.roughness, absCosTheta(w));
    return saturate(bsdfDielectricSpecularF0(material) * ab.x + ab.y);
}

float3 dielectricDirectionalAttenuation(BSDFMaterial material, float3 w) {
    return saturate(1.0 - dielectricDirectionalAlbedo(material, w));
}

float reflectionStackAttenuation(BSDFMaterial material, float viewCoatAttenuation, float3 wi) {
    return viewCoatAttenuation * coatDirectionalAttenuation(material, wi);
}

float transmissionStackAttenuation(float viewSheenAttenuation, float viewCoatAttenuation) {
    return viewSheenAttenuation * viewCoatAttenuation;
}

float materialTransmissionStackAttenuation(BSDFMaterial material, float3 wo) {
    return transmissionStackAttenuation(
        sheenDirectionalAttenuation(bsdfSheenColor(material), wo, material.sheenRoughness),
        coatDirectionalAttenuation(material, wo)
    );
}

BSDFBranchWeights normalizeBranchWeights(BSDFBranchWeights weights) {
    float totalWeight =
        weights.sheen + weights.coat + weights.metal + weights.dielectric + weights.diffuse + weights.subsurface;

    if (totalWeight <= 0.0) {
        weights.diffuse = 1.0;
        weights.sheen = 0.0;
        weights.coat = 0.0;
        weights.metal = 0.0;
        weights.dielectric = 0.0;
        weights.subsurface = 0.0;
        return weights;
    }

    weights.sheen /= totalWeight;
    weights.coat /= totalWeight;
    weights.metal /= totalWeight;
    weights.dielectric /= totalWeight;
    weights.diffuse /= totalWeight;
    weights.subsurface /= totalWeight;

    return weights;
}

BSDFBranchWeights makeBSDFBranchWeights(BSDFMaterial material, float3 wo, uint frontFace) {
    BSDFBranchWeights weights;
    float3 diffuseColor = bsdfDiffuseColor(material);
    float3 sheenColor = bsdfSheenColor(material);
    float nonMetal = 1.0 - material.metallic;
    float baseColorWeight = max(linearSrgbLuminance(material.baseColor), 1e-3);
    float diffuseColorWeight = max(linearSrgbLuminance(diffuseColor), 1e-3);
    float baseScatter = nonMetal * (1.0 - material.transmission);
    float transmissionWeight = material.transmission * max(linearSrgbLuminance(bsdfTransmissionColor(material)), 1e-3);
    float dielectricSpecularWeight =
        nonMetal * max(bsdfDielectricSpecularF0Luminance(material) + transmissionWeight, 0.0);
    weights.sheen = linearSrgbLuminance(sheenColor);

    if (material.transmission > 0.0 && frontFace == 0u) {
        weights.sheen = 0.0;
        weights.coat = 0.0;
        weights.metal = material.metallic * baseColorWeight;
        weights.dielectric = dielectricSpecularWeight;
        weights.diffuse = 0.0;
        weights.subsurface = 0.0;
        return normalizeBranchWeights(weights);
    }

    float viewSheenAttenuation = sheenDirectionalAttenuation(sheenColor, wo, material.sheenRoughness);
    float viewCoatAttenuation = coatDirectionalAttenuation(material, wo);
    float dielectricWeightAttenuation = linearSrgbLuminance(dielectricDirectionalAttenuation(material, wo));

    weights.coat = viewSheenAttenuation * material.clearcoat * 0.25;
    weights.metal = viewSheenAttenuation * viewCoatAttenuation * material.metallic * baseColorWeight;
    weights.dielectric = viewSheenAttenuation * viewCoatAttenuation * dielectricSpecularWeight;
    weights.diffuse = viewSheenAttenuation * viewCoatAttenuation * dielectricWeightAttenuation * baseScatter *
                      (1.0 - material.subsurface) * diffuseColorWeight;
    weights.subsurface = viewSheenAttenuation * viewCoatAttenuation * dielectricWeightAttenuation * baseScatter *
                         material.subsurface * diffuseColorWeight;

    return normalizeBranchWeights(weights);
}

static const float VKRT_DENOISER_SPECULAR_FOLLOW_ROUGHNESS = 0.02;
static const float VKRT_DENOISER_LAYER_EPSILON = 1e-3;

bool materialDenoiserShouldFollowSpecularHit(BSDFMaterial material, uint frontFace) {
    float diffuseWeight = maxComponent(bsdfDiffuseColor(material)) * (1.0 - material.transmission);
    float subsurfaceWeight = diffuseWeight * material.subsurface;
    float sheenLayerWeight = maxComponent(bsdfSheenColor(material)) * (1.0 - material.transmission);
    float clearcoatLayerWeight = frontFace != 0u ? material.clearcoat : 0.0;
    bool hasVisibleBaseLayer =
        diffuseWeight > VKRT_DENOISER_LAYER_EPSILON || subsurfaceWeight > VKRT_DENOISER_LAYER_EPSILON ||
        sheenLayerWeight > VKRT_DENOISER_LAYER_EPSILON || clearcoatLayerWeight > VKRT_DENOISER_LAYER_EPSILON;
    if (hasVisibleBaseLayer) {
        return false;
    }

    if (material.transmission > VKRT_DENOISER_LAYER_EPSILON && dielectricIsIdentity(material)) {
        return true;
    }

    bool hasNearDeltaSpecular = material.roughness <= VKRT_DENOISER_SPECULAR_FOLLOW_ROUGHNESS;
    if (!hasNearDeltaSpecular) {
        return false;
    }

    bool hasSpecularOnlyResponse = material.transmission > VKRT_DENOISER_LAYER_EPSILON ||
                                   material.metallic >= 1.0 - VKRT_DENOISER_LAYER_EPSILON ||
                                   maxComponent(bsdfDielectricSpecularF0(material)) > VKRT_DENOISER_LAYER_EPSILON;
    return hasSpecularOnlyResponse;
}

float3 materialDenoiserAlbedo(BSDFMaterial material) {
    float nonMetal = 1.0 - material.metallic;
    float3 diffuseColor = bsdfDiffuseColor(material);
    float3 sheenColor = bsdfSheenColor(material);
    float3 dielectricF0 = bsdfDielectricSpecularF0(material);
    float3 metallicF0 = materialHasConductor(material)
                          ? fresnelConductor(1.0, max(material.eta, float3(1e-3)), max(material.k, float3(0.0)))
                          : saturate(material.baseColor);

    float diffuseWeight = nonMetal * (1.0 - material.transmission) * (1.0 - material.subsurface);
    float subsurfaceWeight = nonMetal * (1.0 - material.transmission) * material.subsurface;
    float specularWeight = nonMetal * (1.0 - material.transmission) * bsdfDielectricSpecularF0Luminance(material);
    float transmissionWeight = nonMetal * material.transmission;
    float metallicWeight = material.metallic;
    float sheenLayerWeight = maxComponent(sheenColor) * nonMetal * (1.0 - material.transmission);

    float totalWeight =
        diffuseWeight + subsurfaceWeight + specularWeight + transmissionWeight + metallicWeight + sheenLayerWeight;
    if (totalWeight <= 0.0) {
        return saturate(diffuseColor);
    }

    float3 albedo = diffuseColor * diffuseWeight + diffuseColor * subsurfaceWeight + dielectricF0 * specularWeight +
                    float3(1.0) * transmissionWeight + metallicF0 * metallicWeight + sheenColor * sheenLayerWeight;
    return saturate(albedo / totalWeight);
}

BSDFEval evalScalarReflectionStack(BSDFState state, float3 wi, uint spectralMode) {
    BSDFEval eval = {};
    if (cosTheta(wi) <= 0.0) {
        return eval;
    }

    bool interiorInterface = useInteriorDielectricInterface(state);
    float3 sheenValue = float3(0.0);
    float3 coatValue = float3(0.0);
    float3 metalValue = float3(0.0);
    float3 dielectricValue = float3(0.0);
    float3 substrateValue = float3(0.0);
    float3 sheenColor = float3(0.0);

    if (state.sampleWeights.sheen > 0.0 || !interiorInterface) {
        sheenColor = bsdfSheenColor(state.material);
        if (spectralMode != 0u) {
            sheenColor = spectralScalarFromLinearSrgb(saturate(sheenColor), state.wavelengthNm).xxx;
        }
    }

    if (state.sampleWeights.sheen > 0.0) {
        BSDFEval sheen = evalSheen(sheenColor, state.material.sheenRoughness, state.wo, wi);
        sheenValue = sheen.value;
        eval.pdf += state.sampleWeights.sheen * sheen.pdf;
    }

    if (state.sampleWeights.coat > 0.0) {
        BSDFEval coat = evalClearcoat(state.material.clearcoat, state.wo, wi, state.clearcoat);
        coatValue = coat.value;
        eval.pdf += state.sampleWeights.coat * coat.pdf;
    }

    if (state.sampleWeights.metal > 0.0) {
        BSDFEval metal = evalGGX(state.material, state.wo, wi, state.ggx, state.wavelengthNm, spectralMode);
        metalValue = metal.value;
        eval.pdf += state.sampleWeights.metal * metal.pdf;
    }

    if (state.sampleWeights.dielectric > 0.0) {
        BSDFEval dielectric = evalDielectricReflection(
            state.material,
            state.wo,
            wi,
            state.frontFace,
            state.ggx,
            state.wavelengthNm,
            spectralMode
        );
        dielectricValue = dielectric.value;
        eval.pdf += state.sampleWeights.dielectric * dielectric.pdf;
    }

    if (state.sampleWeights.diffuse > 0.0 || state.sampleWeights.subsurface > 0.0) {
        float3 diffuseColor = bsdfDiffuseColor(state.material);
        if (spectralMode != 0u) {
            diffuseColor = spectralScalarFromLinearSrgb(saturate(diffuseColor), state.wavelengthNm).xxx;
        }

        if (state.sampleWeights.diffuse > 0.0) {
            float3 diffuseWeight =
                diffuseColor * ((1.0 - state.material.transmission) * (1.0 - state.material.subsurface));
            BSDFEval diffuse = state.material.diffuseRoughness <= 0.0
                                 ? evalLambertian(diffuseWeight, wi)
                                 : evalOrenNayar(diffuseWeight, state.material.diffuseRoughness, state.wo, wi);
            substrateValue += diffuse.value;
            eval.pdf += state.sampleWeights.diffuse * diffuse.pdf;
        }

        if (state.sampleWeights.subsurface > 0.0) {
            float3 subsurfaceColor = diffuseColor * (1.0 - state.material.transmission);
            BSDFEval subsurface = evalFakeSubsurface(
                subsurfaceColor * state.material.subsurface,
                state.material.diffuseRoughness,
                state.wo,
                wi
            );
            substrateValue += subsurface.value;
            eval.pdf += state.sampleWeights.subsurface * subsurface.pdf;
        }
    }

    if (interiorInterface) {
        float nonMetal = 1.0 - state.material.metallic;
        eval.value = state.material.metallic * metalValue + nonMetal * dielectricValue;
        return eval;
    }

    float viewSheenAttenuation = sheenDirectionalAttenuation(sheenColor, state.wo, state.material.sheenRoughness);
    float viewCoatAttenuation = coatDirectionalAttenuation(state.material, state.wo);
    float3 viewDielectricAttenuation = dielectricDirectionalAttenuation(state.material, state.wo);
    float nonMetal = 1.0 - state.material.metallic;
    float3 baseValue = coatValue + reflectionStackAttenuation(state.material, viewCoatAttenuation, wi) *
                                       (state.material.metallic * metalValue +
                                        nonMetal * (dielectricValue + viewDielectricAttenuation * substrateValue));

    eval.value = sheenValue + viewSheenAttenuation * baseValue;

    return eval;
}

BSDFEval evalBSDF(BSDFState state, float3 wi) {
    if (cosTheta(wi) > 0.0) {
        return evalScalarReflectionStack(state, wi, 0u);
    }

    BSDFEval transmission = evalDielectricTransmission(
        state.material,
        state.wo,
        wi,
        state.frontFace,
        state.ggx,
        useInteriorDielectricInterface(state) ? 1.0 : materialTransmissionStackAttenuation(state.material, state.wo),
        state.wavelengthNm,
        0u
    );

    transmission.pdf *= state.sampleWeights.dielectric;
    return transmission;
}

BSDFEval evalSingleWavelengthBSDF(BSDFState state, float3 wi) {
    if (cosTheta(wi) > 0.0) {
        return evalScalarReflectionStack(state, wi, 1u);
    }

    BSDFEval transmission = evalDielectricTransmission(
        state.material,
        state.wo,
        wi,
        state.frontFace,
        state.ggx,
        useInteriorDielectricInterface(state) ? 1.0 : materialTransmissionStackAttenuation(state.material, state.wo),
        state.wavelengthNm,
        1u
    );

    transmission.pdf *= state.sampleWeights.dielectric;
    return transmission;
}

float4 evalSpectralReflectionStack(BSDFState state, float3 wi, float4 wavelengthsNm, out float4 techniquePdf) {
    techniquePdf = float4(0.0);
    if (cosTheta(wi) <= 0.0) {
        return float4(0.0);
    }

    bool interiorInterface = useInteriorDielectricInterface(state);
    float4 sheenValue = float4(0.0);
    float coatValue = 0.0;
    float4 metalValue = float4(0.0);
    float4 dielectricValue = float4(0.0);
    float4 substrateValue = float4(0.0);
    float4 sheenColor = float4(0.0);

    if (state.sampleWeights.sheen > 0.0 || !interiorInterface) {
        sheenColor = spectralScalarFromLinearSrgb4(saturate(bsdfSheenColor(state.material)), wavelengthsNm);
    }

    if (state.sampleWeights.sheen > 0.0) {
        BSDFEval sheen = evalSheen(float3(1.0), state.material.sheenRoughness, state.wo, wi);
        sheenValue = sheenColor * sheen.value.x;
        techniquePdf += state.sampleWeights.sheen * sheen.pdf;
    }

    if (state.sampleWeights.coat > 0.0) {
        BSDFEval coat = evalClearcoat(state.material.clearcoat, state.wo, wi, state.clearcoat);
        coatValue = coat.value.x;
        techniquePdf += state.sampleWeights.coat * coat.pdf;
    }

    if (state.sampleWeights.metal > 0.0) {
        float metalPdf = 0.0;
        metalValue = evalSpectralGGX(state.material, state.wo, wi, state.ggx, wavelengthsNm, metalPdf);
        techniquePdf += state.sampleWeights.metal * metalPdf;
    }

    if (state.sampleWeights.dielectric > 0.0) {
        float4 dielectricPdf = float4(0.0);
        dielectricValue = evalSpectralDielectricReflection(
            state.material,
            state.wo,
            wi,
            state.frontFace,
            state.ggx,
            wavelengthsNm,
            dielectricPdf
        );
        techniquePdf += state.sampleWeights.dielectric * dielectricPdf;
    }

    if (state.sampleWeights.diffuse > 0.0 || state.sampleWeights.subsurface > 0.0) {
        float4 diffuseColor = spectralScalarFromLinearSrgb4(saturate(bsdfDiffuseColor(state.material)), wavelengthsNm);

        if (state.sampleWeights.diffuse > 0.0) {
            float diffuseScale = (1.0 - state.material.transmission) * (1.0 - state.material.subsurface);
            BSDFEval diffuse = state.material.diffuseRoughness <= 0.0
                                 ? evalLambertian(float3(diffuseScale), wi)
                                 : evalOrenNayar(float3(diffuseScale), state.material.diffuseRoughness, state.wo, wi);
            substrateValue += diffuseColor * diffuse.value.x;
            techniquePdf += state.sampleWeights.diffuse * diffuse.pdf;
        }

        if (state.sampleWeights.subsurface > 0.0) {
            float subsurfaceScale = (1.0 - state.material.transmission) * state.material.subsurface;
            BSDFEval subsurface =
                evalFakeSubsurface(float3(subsurfaceScale), state.material.diffuseRoughness, state.wo, wi);
            substrateValue += diffuseColor * subsurface.value.x;
            techniquePdf += state.sampleWeights.subsurface * subsurface.pdf;
        }
    }

    if (interiorInterface) {
        float nonMetal = 1.0 - state.material.metallic;
        return state.material.metallic * metalValue + nonMetal * dielectricValue;
    }

    float viewCoatAttenuation = coatDirectionalAttenuation(state.material, state.wo);
    float4 viewSheenAttenuation =
        saturate(1.0 - sheenColor * sheenDirectionalAlbedo(cosTheta(state.wo), state.material.sheenRoughness));
    float4 viewDielectricAttenuation = spectralScalarFromLinearSrgb4(
        saturate(dielectricDirectionalAttenuation(state.material, state.wo)),
        wavelengthsNm
    );
    float nonMetal = 1.0 - state.material.metallic;
    float reflectionAttenuation = reflectionStackAttenuation(state.material, viewCoatAttenuation, wi);
    float4 baseValue = coatValue.xxxx + reflectionAttenuation *
                                            (state.material.metallic * metalValue +
                                             nonMetal * (dielectricValue + viewDielectricAttenuation * substrateValue));

    return sheenValue + viewSheenAttenuation * baseValue;
}

float4 evalSpectralBSDF(BSDFState state, float3 wi, float4 wavelengthsNm, out float4 techniquePdf) {
    if (cosTheta(wi) > 0.0) {
        return evalSpectralReflectionStack(state, wi, wavelengthsNm, techniquePdf);
    }

    float coatAttenuation =
        useInteriorDielectricInterface(state) ? 1.0 : materialTransmissionStackAttenuation(state.material, state.wo);
    float4 value = evalSpectralDielectricTransmission(
        state.material,
        state.wo,
        wi,
        state.frontFace,
        state.ggx,
        coatAttenuation,
        wavelengthsNm,
        techniquePdf
    );
    techniquePdf *= state.sampleWeights.dielectric;
    return value;
}

bool sampleBSDFDirection(BSDFState state, inout uint rng, out BSDFDirectionSample sample) {
    sample = {};

    float selector = rand(rng);
    if (selector < state.sampleWeights.sheen) {
        return sampleSheen(state.wo, state.material.sheenRoughness, rng, sample.wi);
    }

    selector -= state.sampleWeights.sheen;
    if (selector < state.sampleWeights.coat) {
        return sampleClearcoat(state.wo, state.clearcoat, rng, sample.wi);
    }

    selector -= state.sampleWeights.coat;
    if (selector < state.sampleWeights.metal) {
        return sampleGGX(state.wo, state.ggx, rng, sample.wi);
    }

    selector -= state.sampleWeights.metal;
    if (selector < state.sampleWeights.dielectric) {
        DielectricSample dielectricSample;
        if (!sampleDielectric(
                state.material,
                state.wo,
                state.frontFace,
                state.ggx,
                state.wavelengthNm,
                state.spectralMode,
                rng,
                dielectricSample
            )) {
            return false;
        }

        sample.wi = dielectricSample.wi;
        sample.isTransmission = dielectricSample.isTransmission;
        return true;
    }

    selector -= state.sampleWeights.dielectric;
    if (selector < state.sampleWeights.diffuse) {
        sample.wi = state.material.diffuseRoughness <= 0.0 ? sampleLambertian(rng) : sampleOrenNayar(rng);
        return true;
    }

    sample.wi = sampleFakeSubsurface(rng);
    return true;
}

#endif
