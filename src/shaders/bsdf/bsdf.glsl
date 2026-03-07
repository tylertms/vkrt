#ifndef SHADER_BSDF_GLSL
#define SHADER_BSDF_GLSL

#include "core/types.glsl"
#include "core/math.glsl"
#include "utility/rand.glsl"

struct BSDFSample {
    vec3 incoming;
    vec3 f;
    float pdf;
};

// https://media.disneyanimation.com/uploads/production/publication_asset/48/asset/s2012_pbs_disney_brdf_notes_v3.pdf
// https://learnopengl.com/PBR/Theory

vec3 sampleCosineHemisphere(vec3 normal, inout uint state) {
    float r1 = rand(state);
    float r2 = rand(state);
    float r = sqrt(r1);
    float phi = TWO_PI * r2;

    vec3 local = vec3(r * cos(phi), r * sin(phi), sqrt(max(1.0 - r1, 0.0)));
    return buildTBN(normal) * local;
}

float cosineHemispherePdf(vec3 normal, vec3 incoming) {
    return max(dot(normal, incoming), 0.0) * INV_PI;
}

float iorF0(float ior) {
    float r = (ior - 1.0) / (ior + 1.0);
    return r * r;
}

vec3 fresnelSchlick(float cosTheta, Material material) {
    vec3 f0 = mix(vec3(iorF0(material.ior)), material.baseColor, material.metallic);
    float f = 1.0 - cosTheta;
    float f2 = f * f;
    return f0 + (1.0 - f0) * f2 * f2 * f;
}

vec3 evalBSDF(vec3 normal, vec3 incoming, vec3 outgoing, Material material) {
    float cosNL = max(dot(normal, incoming), 0.0);
    if (cosNL <= 0.0 || dot(normal, outgoing) <= 0.0) return vec3(0.0);

    vec3 diffuseColor = material.baseColor * (1.0 - material.metallic);
    return diffuseColor * INV_PI;
}

float pdfBSDF(vec3 normal, vec3 incoming, vec3 outgoing, Material material) {
    return cosineHemispherePdf(normal, incoming);
}

BSDFSample sampleBSDF(vec3 normal, vec3 outgoing, Material material, inout uint state) {
    BSDFSample result;
    result.incoming = sampleCosineHemisphere(normal, state);
    result.f = evalBSDF(normal, result.incoming, outgoing, material);
    result.pdf = pdfBSDF(normal, result.incoming, outgoing, material);
    return result;
}

#endif
