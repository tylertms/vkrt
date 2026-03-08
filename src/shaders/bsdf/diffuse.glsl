#ifndef SHADER_BSDF_DIFFUSE_GLSL
#define SHADER_BSDF_DIFFUSE_GLSL

vec3 evalImprovedOrenNayar(vec3 albedo, float roughness, float NdotL, float NdotV, float LdotV) {
    float s = LdotV - NdotL * NdotV;
    float t = s <= 0.0 ? 1.0 : max(NdotL, NdotV);
    float denom = PI + (0.5 * PI - 2.0 / 3.0) * roughness;
    float A = 1.0 / denom;
    float B = roughness / denom;
    return albedo * (A + B * (s / t));
}

#endif
