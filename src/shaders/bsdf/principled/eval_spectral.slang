#ifndef VKRT_BSDF_PRINCIPLED_EVAL_SPECTRAL_SLANG
#define VKRT_BSDF_PRINCIPLED_EVAL_SPECTRAL_SLANG

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

#endif
