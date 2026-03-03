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
    float alpha = max(material.specularRoughness * material.specularRoughness, 0.001);
    vec3 f0 = mix(vec3(0.04), material.baseColor, material.baseMetalness);

    vec3 diffuse = (1.0 - material.baseMetalness) * evalDiffuse(material.baseColor);
    vec3 specular = evalGGX(normal, incoming, outgoing, alpha, f0);

    return diffuse + specular;
}

float pdfBSDF(vec3 normal, vec3 incoming, vec3 outgoing, Material material) {
    float alpha = max(material.specularRoughness * material.specularRoughness, 0.001);
    float specWeight = specularSampleWeight(material);

    float diffuse = (1.0 - specWeight) * pdfDiffuse(normal, incoming);
    float specular = specWeight * pdfGGX(normal, incoming, outgoing, alpha);

    return diffuse + specular;
}

BSDFSample sampleBSDF(vec3 normal, vec3 outgoing, Material material, inout uint state) {
    float alpha = max(material.specularRoughness * material.specularRoughness, 0.001);
    float specWeight = specularSampleWeight(material);
    vec3 f0 = mix(vec3(0.04), material.baseColor, material.baseMetalness);

    BSDFSample result;

    if (rand(state) < specWeight) {
        result.incoming = sampleGGX(normal, outgoing, alpha, state);
    } else {
        result.incoming = sampleDiffuse(normal, state);
    }

    vec3 diffuseF = (1.0 - material.baseMetalness) * evalDiffuse(material.baseColor);
    vec3 specularF = evalGGX(normal, result.incoming, outgoing, alpha, f0);
    result.f = diffuseF + specularF;

    float diffusePdf = (1.0 - specWeight) * pdfDiffuse(normal, result.incoming);
    float specularPdf = specWeight * pdfGGX(normal, result.incoming, outgoing, alpha);
    result.pdf = diffusePdf + specularPdf;

    return result;
}

#endif
