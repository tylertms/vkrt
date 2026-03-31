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

#endif
