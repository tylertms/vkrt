#ifndef VKRT_BSDF_MODEL_SLANG
#define VKRT_BSDF_MODEL_SLANG

#include "base.slang"
#include "lobes/lambertian.slang"
#include "lobes/oren_nayar.slang"
#include "lobes/subsurface.slang"
#include "lobes/sheen.slang"
#include "lobes/ggx.slang"
#include "lobes/clearcoat.slang"
#include "lobes/dielectric.slang"

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
    Material material;
    float3 wo;
    float3 diffuseColor;
    float3 subsurfaceColor;
    float3 sheenColor;
    GGXParams ggx;
    ClearcoatParams clearcoat;
    uint frontFace;
    BSDFBranchWeights sampleWeights;
};

bool useInteriorDielectricInterface(BSDFState state) {
    return state.material.transmission > 0.0 && state.frontFace == 0u;
}

float scalarSchlickFresnel(float cosThetaValue, float f0) {
    return lerp(f0, 1.0, schlickWeight(abs(cosThetaValue)));
}

float coatDirectionalAttenuation(Material material, float3 w) {
    if (material.clearcoat <= 0.0) {
        return 1.0;
    }

    if (absCosTheta(w) <= 0.0) {
        return 0.0;
    }

    float coatFresnel = 0.25 * material.clearcoat * scalarSchlickFresnel(absCosTheta(w), 0.04);
    return 1.0 - saturate(coatFresnel);
}

float coatLayerReflectionAttenuation(BSDFState state, float3 wi) {
    return coatDirectionalAttenuation(state.material, state.wo) * coatDirectionalAttenuation(state.material, wi);
}

float coatLayerTransmissionAttenuation(BSDFState state) {
    return coatDirectionalAttenuation(state.material, state.wo);
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

float3 dielectricDirectionalAlbedo(Material material, float3 w) {
    if (absCosTheta(w) <= 0.0) {
        return float3(1.0);
    }

    float2 ab = dielectricDirectionalAlbedoAB(material.roughness, absCosTheta(w));
    return saturate(bsdfDielectricSpecularF0(material) * ab.x + ab.y);
}

float3 dielectricDirectionalAttenuation(Material material, float3 w) {
    return saturate(1.0 - dielectricDirectionalAlbedo(material, w));
}

float dielectricDirectionalWeightAttenuation(Material material, float3 w) {
    float3 attenuation = dielectricDirectionalAttenuation(material, w);
    return saturate(bsdfLuminance(attenuation));
}

BSDFBranchWeights normalizeBranchWeights(BSDFBranchWeights weights) {
    float totalWeight = weights.sheen + weights.coat + weights.metal +
        weights.dielectric + weights.diffuse + weights.subsurface;

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

BSDFBranchWeights makeBSDFBranchWeights(Material material, float3 wo, float3 diffuseColor, float3 sheenColor, uint frontFace) {
    BSDFBranchWeights weights = BSDFBranchWeights();
    float nonMetal = 1.0 - material.metallic;
    float baseColorWeight = max(bsdfLuminance(material.baseColor), 1e-3);
    float diffuseColorWeight = max(bsdfLuminance(diffuseColor), 1e-3);
    float baseScatter = nonMetal * (1.0 - material.transmission);
    float transmissionWeight = material.transmission * max(bsdfLuminance(bsdfTransmissionColor(material)), 1e-3);

    weights.sheen = bsdfLuminance(sheenColor);

    if (material.transmission > 0.0 && frontFace == 0u) {
        weights.sheen = 0.0;
        weights.coat = 0.0;
        weights.metal = material.metallic * baseColorWeight;
        weights.dielectric = nonMetal * max(bsdfDielectricSpecularF0Luminance(material) + transmissionWeight, 0.0);
        weights.diffuse = 0.0;
        weights.subsurface = 0.0;
        return normalizeBranchWeights(weights);
    }

    float sheenAttenuation = sheenDirectionalAttenuation(sheenColor, wo, material.sheenRoughness);
    float coatAttenuation = coatDirectionalAttenuation(material, wo);
    float dielectricAttenuation = dielectricDirectionalWeightAttenuation(material, wo);

    weights.coat = sheenAttenuation * material.clearcoat * 0.25;
    weights.metal = sheenAttenuation * coatAttenuation * material.metallic * baseColorWeight;
    weights.dielectric = sheenAttenuation * coatAttenuation * nonMetal * max(bsdfDielectricSpecularF0Luminance(material) + transmissionWeight, 0.0);
    weights.diffuse = sheenAttenuation * coatAttenuation * dielectricAttenuation *
        baseScatter * (1.0 - material.subsurface) * diffuseColorWeight;
    weights.subsurface = sheenAttenuation * coatAttenuation * dielectricAttenuation *
        baseScatter * material.subsurface * diffuseColorWeight;

    return normalizeBranchWeights(weights);
}

BSDFState makeBSDFState(Material material, float3 wo, uint frontFace) {
    BSDFState state = BSDFState();
    state.material = material;
    state.wo = wo;
    state.frontFace = frontFace;
    state.diffuseColor = bsdfDiffuseColor(material);
    state.subsurfaceColor = state.diffuseColor * (1.0 - material.transmission);
    state.sheenColor = bsdfSheenColor(material);
    state.ggx = makeGGXParams(material, wo);
    state.clearcoat = makeClearcoatParams(material);
    state.sampleWeights = makeBSDFBranchWeights(material, wo, state.diffuseColor, state.sheenColor, frontFace);
    return state;
}

BSDFEval evalReflectionStack(BSDFState state, float3 wi) {
    BSDFEval eval = BSDFEval();
    if (cosTheta(wi) <= 0.0) {
        return eval;
    }

    BSDFEval sheen = BSDFEval();
    BSDFEval coat = BSDFEval();
    BSDFEval metal = BSDFEval();
    BSDFEval dielectric = BSDFEval();
    BSDFEval diffuse = BSDFEval();
    BSDFEval subsurface = BSDFEval();

    if (state.sampleWeights.sheen > 0.0) {
        sheen = evalSheen(state.sheenColor, state.material.sheenRoughness, state.wo, wi);
    }

    if (state.sampleWeights.coat > 0.0) {
        coat = evalClearcoat(state.material.clearcoat, state.wo, wi, state.clearcoat);
    }

    if (state.sampleWeights.metal > 0.0) {
        metal = evalGGX(state.material, state.wo, wi, state.ggx);
    }

    if (state.sampleWeights.dielectric > 0.0) {
        dielectric = evalDielectricReflection(state.material, state.wo, wi, state.frontFace, state.ggx);
    }

    if (state.sampleWeights.diffuse > 0.0) {
        float3 diffuseWeight = state.diffuseColor * ((1.0 - state.material.transmission) * (1.0 - state.material.subsurface));
        if (state.material.diffuseRoughness <= 0.0) {
            diffuse = evalLambertian(diffuseWeight, wi);
        } else {
            diffuse = evalOrenNayar(diffuseWeight, state.material.diffuseRoughness, state.wo, wi);
        }
    }

    if (state.sampleWeights.subsurface > 0.0) {
        subsurface = evalFakeSubsurface(
            state.subsurfaceColor * state.material.subsurface,
            state.material.diffuseRoughness,
            state.wo,
            wi);
    }

    if (useInteriorDielectricInterface(state)) {
        eval.value = state.material.metallic * metal.value +
            (1.0 - state.material.metallic) * dielectric.value;
        eval.pdf = state.sampleWeights.metal * metal.pdf +
            state.sampleWeights.dielectric * dielectric.pdf;
        return eval;
    }

    float coatAttenuation = coatLayerReflectionAttenuation(state, wi);
    float3 dielectricAttenuation = dielectricDirectionalAttenuation(state.material, state.wo);
    float sheenAttenuation = sheenDirectionalAttenuation(state.sheenColor, state.wo, state.material.sheenRoughness);
    float3 baseValue = coat.value + coatAttenuation * (
        state.material.metallic * metal.value +
        (1.0 - state.material.metallic) * (dielectric.value + dielectricAttenuation * (diffuse.value + subsurface.value))
    );

    eval.value = sheen.value + sheenAttenuation * baseValue;

    eval.pdf = state.sampleWeights.sheen * sheen.pdf +
        state.sampleWeights.coat * coat.pdf +
        state.sampleWeights.metal * metal.pdf +
        state.sampleWeights.dielectric * dielectric.pdf +
        state.sampleWeights.diffuse * diffuse.pdf +
        state.sampleWeights.subsurface * subsurface.pdf;

    return eval;
}

BSDFEval evalBSDF(BSDFState state, float3 wi) {
    if (cosTheta(wi) > 0.0) {
        return evalReflectionStack(state, wi);
    }

    BSDFEval transmission = evalDielectricTransmission(
        state.material,
        state.wo,
        wi,
        state.frontFace,
        state.ggx,
        useInteriorDielectricInterface(state) ? 1.0 :
            coatLayerTransmissionAttenuation(state) * sheenDirectionalAttenuation(state.sheenColor, state.wo, state.material.sheenRoughness)
    );

    transmission.pdf *= state.sampleWeights.dielectric;
    return transmission;
}

bool sampleBSDFDirection(BSDFState state, inout uint rng, out BSDFDirectionSample sample) {
    sample = BSDFDirectionSample();

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
                rng,
                dielectricSample)) {
            return false;
        }

        sample.wi = dielectricSample.wi;
        sample.isTransmission = dielectricSample.isTransmission;
        return true;
    }

    selector -= state.sampleWeights.dielectric;
    if (selector < state.sampleWeights.diffuse) {
        sample.wi = state.material.diffuseRoughness <= 0.0
            ? sampleLambertian(rng)
            : sampleOrenNayar(rng);
        return true;
    }

    sample.wi = sampleFakeSubsurface(rng);
    return true;
}

#endif
