#ifndef SHADER_BSDF_GLSL
#define SHADER_BSDF_GLSL

#include "core/types.glsl"
#include "core/math.glsl"
#include "utility/material.glsl"
#include "bsdf/diffuse.glsl"
#include "bsdf/ggx.glsl"

struct BSDFSample {
    vec3 incoming;
    vec3 f;
    float pdf;
};

vec3 evalBSDF(vec3 normal, vec3 incoming, vec3 outgoing, Material material) {
    float cosNL = max(dot(normal, incoming), 0.0);
    float cosNV = max(dot(normal, outgoing), 0.0);
    if (cosNL <= 0.0 || cosNV <= 0.0) return vec3(0.0);

    MaterialBSDFState state = buildMaterialBSDFState(material);

    vec3 specular = evalGGX(normal, incoming, outgoing, state.alpha, state.f0);

    vec3 h = normalize(incoming + outgoing);
    float cosVH = max(dot(outgoing, h), 0.0);
    vec3 F = F_Schlick(cosVH, state.f0);
    vec3 diffuse = state.diffuseColor * (vec3(1.0) - F) * INV_PI;

    return diffuse + specular;
}

float pdfBSDF(vec3 normal, vec3 incoming, vec3 outgoing, Material material) {
    MaterialBSDFState state = buildMaterialBSDFState(material);
    float specWeight = specularSampleWeight(state);

    float diffuse = (1.0 - specWeight) * pdfDiffuse(normal, incoming);
    float specular = specWeight * pdfGGX(normal, incoming, outgoing, state.alpha);

    return diffuse + specular;
}

BSDFSample sampleBSDF(vec3 normal, vec3 outgoing, Material material, inout uint state) {
    MaterialBSDFState materialState = buildMaterialBSDFState(material);
    float specWeight = specularSampleWeight(materialState);

    BSDFSample result;

    if (specWeight <= 1e-6) {
        result.incoming = sampleDiffuse(normal, state);
    } else if (specWeight >= 1.0 - 1e-6) {
        result.incoming = sampleGGX(normal, outgoing, materialState.alpha, state);
    } else if (rand(state) < specWeight) {
        result.incoming = sampleGGX(normal, outgoing, materialState.alpha, state);
    } else {
        result.incoming = sampleDiffuse(normal, state);
    }

    result.f = evalBSDF(normal, result.incoming, outgoing, material);

    float diffusePdf = (1.0 - specWeight) * pdfDiffuse(normal, result.incoming);
    float specularPdf = specWeight * pdfGGX(normal, result.incoming, outgoing, materialState.alpha);
    result.pdf = diffusePdf + specularPdf;

    return result;
}

#endif
