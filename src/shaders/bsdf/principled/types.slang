#ifndef VKRT_BSDF_PRINCIPLED_TYPES_SLANG
#define VKRT_BSDF_PRINCIPLED_TYPES_SLANG

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
    float3 geometricNormal;
    GGXParams ggx;
    ClearcoatParams clearcoat;
    uint frontFace;
    float wavelengthNm;
    uint spectralMode;
    BSDFBranchWeights sampleWeights;

    __init(
        BSDFMaterial material,
        float3 wo,
        float3 geometricNormal,
        uint frontFace,
        float4 wavelengthsNm,
        uint spectralMode
    ) {
        this.material = material;
        this.wo = wo;
        this.geometricNormal = geometricNormal;
        this.frontFace = frontFace;
        this.wavelengthNm = wavelengthsNm.x;
        this.spectralMode = spectralMode;
        ggx = makeGGXParams(material, wo);
        cacheDielectricGGXEnergy(ggx, material, wo, frontFace, wavelengthsNm, spectralMode);
        clearcoat = makeClearcoatParams(material);
        sampleWeights = makeBSDFBranchWeights(material, wo, frontFace, ggx);
    }
};

bool bsdfDirectionIsValid(BSDFState state, float3 wi) {
    return cosTheta(state.wo) > 0.0 && dot(state.wo, state.geometricNormal) > 0.0 &&
           cosTheta(wi) * dot(wi, state.geometricNormal) > 0.0;
}

#endif
