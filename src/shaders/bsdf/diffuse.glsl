#ifndef SHADER_BSDF_DIFFUSE_GLSL
#define SHADER_BSDF_DIFFUSE_GLSL

const float kEonFONConstant1 = 0.5 - 2.0 / (3.0 * PI);
const float kEonFONConstant2 = 2.0 / 3.0 - 28.0 / (15.0 * PI);

float eonDirectionalAlbedoApprox(float mu, float roughness) {
    float muComp = 1.0 - clamp(mu, 0.0, 1.0);
    float g1 = 0.0571085289;
    float g2 = 0.491881867;
    float g3 = -0.332181442;
    float g4 = 0.0714429953;
    float GOverPi = muComp * (g1 + muComp * (g2 + muComp * (g3 + muComp * g4)));
    return (1.0 + roughness * GOverPi) / (1.0 + kEonFONConstant1 * roughness);
}

vec3 evalEONDiffuse(vec3 albedo, float roughness, float NdotL, float NdotV, float LdotV) {
    float s = LdotV - NdotL * NdotV;
    float sOverTF = s > 0.0 ? s / max(NdotL, NdotV) : s;
    float AF = 1.0 / (1.0 + kEonFONConstant1 * roughness);
    vec3 singleScatter = (albedo * INV_PI) * AF * (1.0 + roughness * sOverTF);

    float EFi = eonDirectionalAlbedoApprox(NdotL, roughness);
    float EFo = eonDirectionalAlbedoApprox(NdotV, roughness);
    float avgEF = AF * (1.0 + kEonFONConstant2 * roughness);
    vec3 multiScatterAlbedo = (albedo * albedo) * avgEF /
            max(vec3(1.0) - albedo * (1.0 - avgEF), vec3(1e-4));
    vec3 multiScatter = (multiScatterAlbedo * INV_PI) *
            max(1.0 - EFo, 1e-4) * max(1.0 - EFi, 1e-4) / max(1.0 - avgEF, 1e-4);

    return singleScatter + multiScatter;
}

#endif
