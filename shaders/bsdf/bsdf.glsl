#ifndef SHADER_BSDF_GLSL
#define SHADER_BSDF_GLSL

#include "core/types.glsl"
#include "core/math.glsl"
#include "bsdf/diffuse.glsl"
#include "bsdf/ggx.glsl"

vec3 sampleBSDF(vec3 normal, vec3 outgoing, Material material, inout uint state) {
    float alpha = max(material.roughness * material.roughness, 0.001);
    vec3 f0 = mix(vec3(0.04), material.baseColor, material.metallic);
    float specularWeight = clamp(luminance(f0), 0.1, 0.9);

    if (rand(state) < specularWeight) {
        return sampleGGX(normal, outgoing, alpha, state);
    } else {
        return sampleDiffuse(normal, state);
    }
}

vec3 evalBSDF(vec3 normal, vec3 incoming, vec3 outgoing, Material material) {
    float alpha = max(material.roughness * material.roughness, 0.001);
    vec3 f0 = mix(vec3(0.04), material.baseColor, material.metallic);

    vec3 diffuse = (1.0 - material.metallic) * evalDiffuse(material.baseColor);
    vec3 specular = evalGGX(normal, incoming, outgoing, alpha, f0);

    return diffuse + specular;
}

float pdfBSDF(vec3 normal, vec3 incoming, vec3 outgoing, Material material) {
    float alpha = max(material.roughness * material.roughness, 0.001);
    vec3 f0 = mix(vec3(0.04), material.baseColor, material.metallic);
    float specularWeight = clamp(luminance(f0), 0.1, 0.9);

    float diffuse = (1.0 - specularWeight) * pdfDiffuse(normal, incoming);
    float specular = specularWeight * pdfGGX(normal, incoming, outgoing, alpha);

    return diffuse + specular;
}

#endif
