#ifndef VKRT_BSDF_BASE_INTERFACE_SLANG
#define VKRT_BSDF_BASE_INTERFACE_SLANG

float interfaceIor(BSDFMaterial material) {
    return max(material.ior, 1.0);
}

float interfaceIor(BSDFMaterial material, float lambdaNm, uint spectralMode) {
    float ior = interfaceIor(material);
    return spectralMode != 0u ? dispersiveIor(ior, material.abbeNumber, lambdaNm) : ior;
}

float interfaceEta(BSDFMaterial material, uint frontFace) {
    float ior = interfaceIor(material);
    return frontFace != 0u ? ior : (1.0 / ior);
}

float interfaceEta(BSDFMaterial material, uint frontFace, float lambdaNm, uint spectralMode) {
    float ior = interfaceIor(material, lambdaNm, spectralMode);
    return frontFace != 0u ? ior : (1.0 / ior);
}

float interfaceRefractionEta(BSDFMaterial material, uint frontFace) {
    float ior = interfaceIor(material);
    return frontFace != 0u ? (1.0 / ior) : ior;
}

float interfaceRefractionEta(BSDFMaterial material, uint frontFace, float lambdaNm, uint spectralMode) {
    float ior = interfaceIor(material, lambdaNm, spectralMode);
    return frontFace != 0u ? (1.0 / ior) : ior;
}

#endif
