#ifndef VKRT_BSDF_PRINCIPLED_EVAL_RGB_SLANG
#define VKRT_BSDF_PRINCIPLED_EVAL_RGB_SLANG

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

#endif
