#ifndef VKRT_BSDF_MODEL_SLANG
#define VKRT_BSDF_MODEL_SLANG

#include "common.slang"
#include "lobes/lambertian.slang"
#include "lobes/ggx.slang"

// References:
// BSDF representation: https://www.pbr-book.org/4ed/Reflection_Models/BSDF_Representation
// Mixture sampling heuristic: https://www.pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency
// Path tracer throughput/pdf: https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer

struct BSDFWeights {
    float diffuse;
    float specular;
};

struct BSDFState {
    Material material;
    float3 wo;
    float3 diffuseColor;
    GGXParams ggx;
    BSDFWeights weights;
};

BSDFWeights makeBSDFWeights(float3 diffuseColor, Material material) {
    BSDFWeights weights;
    float diffuseWeight = bsdfLuminance(diffuseColor);
    float specularWeight = bsdfLuminance(bsdfSpecularF0(material));
    float totalWeight = diffuseWeight + specularWeight;
    if (totalWeight <= 0.0) {
        weights.diffuse = 1.0;
        weights.specular = 0.0;
        return weights;
    }

    weights.diffuse = diffuseWeight / totalWeight;
    weights.specular = specularWeight / totalWeight;
    return weights;
}

BSDFState makeBSDFState(Material material, float3 wo) {
    BSDFState state;
    state.material = material;
    state.wo = wo;
    state.diffuseColor = bsdfDiffuseColor(material);
    state.ggx = makeGGXParams(material, wo);
    state.weights = makeBSDFWeights(state.diffuseColor, material);
    return state;
}

BSDFEval evalBSDF(BSDFState state, float3 wi) {
    BSDFEval diffuse = evalLambertian(state.diffuseColor, wi);
    BSDFEval specular = evalGGX(state.material, state.wo, wi, state.ggx);

    BSDFEval eval;
    eval.value = diffuse.value + specular.value;
    eval.pdf = state.weights.diffuse * diffuse.pdf + state.weights.specular * specular.pdf;
    return eval;
}

bool sampleBSDFDirection(BSDFState state, inout uint rng, out float3 wi) {
    if (rand(rng) < state.weights.specular) {
        return sampleGGX(state.wo, state.ggx, rng, wi);
    }

    wi = sampleLambertian(rng);
    return true;
}

#endif
