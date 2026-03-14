#ifndef VKRT_BSDF_BSDF_SLANG
#define VKRT_BSDF_BSDF_SLANG

#include "common.slang"
#include "lobes/lambertian.slang"
#include "lobes/ggx.slang"

// References:
// BSDF representation: https://www.pbr-book.org/4ed/Reflection_Models/BSDF_Representation
// Mixture sampling heuristic: https://www.pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency
// Path tracer throughput/pdf: https://www.pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer

struct BSDFState {
    float3 wo;
    float2 ggxAlpha;
    float ggxK;
    float ggxProjectedLength;
    float3 diffuseColor;
    float diffuseProbability;
    float specularProbability;
};

BSDFState makeBSDFState(BSDFInput input) {
    BSDFState state;
    state.wo = worldToLocal(input.wo, input.normal);
    state.ggxAlpha = ggxAlpha(input.material);
    state.ggxK = ggxBoundedVNDFK(state.wo, state.ggxAlpha);
    state.ggxProjectedLength = ggxProjectedLength(state.wo, state.ggxAlpha);
    state.diffuseColor = bsdfDiffuseColor(input.material);

    float diffuseWeight = bsdfLuminance(state.diffuseColor);
    float specularWeight = bsdfLuminance(bsdfSpecularF0(input.material));
    float totalWeight = diffuseWeight + specularWeight;
    if (totalWeight <= 0.0) {
        state.diffuseProbability = 1.0;
        state.specularProbability = 0.0;
        return state;
    }

    state.specularProbability = specularWeight / totalWeight;
    state.diffuseProbability = 1.0 - state.specularProbability;
    return state;
}

void bsdfAccumulateLobe(inout float3 value, inout float pdf, float probability, float3 lobeValue, float lobePdf) {
    value += lobeValue;
    pdf += probability * lobePdf;
}

BSDFSample sampleBSDF(BSDFInput input, inout uint rng) {
    BSDFSample sample = BSDFSample();

    BSDFState state = makeBSDFState(input);
    if (state.wo.z <= 0.0) return sample;

    float3 wi;
    float3 ggxValue;
    float ggxPdf;

    if (rand(rng) < state.specularProbability) {
        float3 m;
        float woDotM;
        if (!sampleGGXDirectionLocal(state.wo, state.ggxAlpha, state.ggxK, state.ggxProjectedLength, rng, wi, m, woDotM)) {
            return sample;
        }

        ggxEvalPdfFromSample(input, state.wo, wi, state.ggxAlpha, m, woDotM, state.ggxK, state.ggxProjectedLength, ggxValue, ggxPdf);
    } else {
        wi = sampleLambertianDirectionLocal(rng);
        ggxEvalPdfLocal(input, state.wo, wi, state.ggxAlpha, state.ggxK, state.ggxProjectedLength, ggxValue, ggxPdf);
    }

    float3 lambertValue = wi.z > 0.0 ? state.diffuseColor * VKRT_INV_PI : float3(0.0);
    float lambertPdf = pdfLambertianLocal(wi);
    sample.value = float3(0.0);
    sample.pdf = 0.0;
    bsdfAccumulateLobe(sample.value, sample.pdf, state.diffuseProbability, lambertValue, lambertPdf);
    bsdfAccumulateLobe(sample.value, sample.pdf, state.specularProbability, ggxValue, ggxPdf);
    if (sample.pdf <= 0.0) {
        return sample;
    }

    sample.wi = localToWorld(wi, input.normal);
    sample.weight = sample.value * wi.z / sample.pdf;
    return sample;
}

#endif
