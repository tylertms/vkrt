#ifndef SHADER_BSDF_COMMON_GLSL
#define SHADER_BSDF_COMMON_GLSL

float sqr(float x) {
    return x * x;
}

float schlickFresnel(float u) {
    float m = clamp(1.0 - u, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}

mat3 shadingBasis(vec3 normal) {
    return buildTBN(normal);
}

SurfaceState makeSurfaceState(Material material) {
    SurfaceState state;

    float baseLum = luminance(material.baseColor);
    vec3 tint = baseLum > 0.0 ? material.baseColor / baseLum : vec3(1.0);

    vec3 dielectric = material.specular * 0.08 * mix(vec3(1.0), tint, material.specularTint);
    state.spec0 = mix(dielectric, material.baseColor, material.metallic);
    state.sheenColor = mix(vec3(1.0), tint, material.sheenTint);

    state.diffuseWeight = (1.0 - material.metallic) * max(baseLum, 0.05);
    state.specularWeight = luminance(state.spec0) * mix(8.0, 1.0, material.roughness);
    state.clearcoatWeight = 0.25 * material.clearcoat * mix(1.0, 4.0, material.clearcoatGloss);
    state.sheenWeight = (1.0 - material.metallic) * material.sheen * 0.25;

    float sum = state.diffuseWeight + state.specularWeight + state.clearcoatWeight + state.sheenWeight;
    if (sum <= 1e-6) {
        state.diffuseWeight = 1.0;
        state.specularWeight = 0.0;
        state.clearcoatWeight = 0.0;
        state.sheenWeight = 0.0;
    } else {
        float invSum = 1.0 / sum;
        state.diffuseWeight *= invSum;
        state.specularWeight *= invSum;
        state.clearcoatWeight *= invSum;
        state.sheenWeight *= invSum;
    }

    float roughness = max(material.roughness, 0.001);
    float roughness2 = roughness * roughness;
    float aspect = sqrt(max(0.1, 1.0 - material.anisotropic * 0.9));
    state.diffuseRoughness = material.roughness;
    state.ax = max(0.001, roughness2 / aspect);
    state.ay = max(0.001, roughness2 * aspect);
    state.clearcoatAlpha = mix(0.1, 0.001, material.clearcoatGloss);
    state.sheenRoughness = clamp(material.roughness, 0.07, 1.0);
    return state;
}

vec3 fresnelSchlick(float cosTheta, Material material) {
    return mix(makeSurfaceState(material).spec0, vec3(1.0), schlickFresnel(cosTheta));
}

vec3 sampleCosineHemisphere(vec3 normal, inout uint state) {
    float r1 = rand(state);
    float r2 = rand(state);
    float r = sqrt(r1);
    float phi = TWO_PI * r2;
    vec3 local = vec3(r * cos(phi), r * sin(phi), sqrt(max(1.0 - r1, 0.0)));
    return shadingBasis(normal) * local;
}

vec3 sampleUniformHemisphereLocal(float u1, float u2) {
    float z = u1;
    float phi = TWO_PI * u2;
    float sinTheta = sqrt(max(1.0 - z * z, 0.0));
    return vec3(sinTheta * cos(phi), sinTheta * sin(phi), z);
}

#endif
