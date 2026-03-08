#ifndef SHADER_BSDF_SHEEN_GLSL
#define SHADER_BSDF_SHEEN_GLSL

float sheenLambda(float cosTheta, float roughness) {
    float blend = sqr(1.0 - roughness);
    float a = mix(21.5473, 25.3245, blend);
    float b = mix(3.82987, 3.32435, blend);
    float c = mix(0.19823, 0.16801, blend);
    float d = mix(-1.97760, -1.27393, blend);
    float e = mix(-4.32054, -4.85967, blend);

    float x = abs(cosTheta);
    float l;
    if (x < 0.5) {
        l = a / (1.0 + b * pow(x, c)) + d * x + e;
    } else {
        float l0 = a / (1.0 + b * pow(0.5, c)) + d * 0.5 + e;
        float xr = 1.0 - x;
        l = 2.0 * l0 - (a / (1.0 + b * pow(xr, c)) + d * xr + e);
    }
    return exp(l);
}

float DCharlie(float NdotH, float roughness) {
    float sinTheta = sqrt(max(1.0 - NdotH * NdotH, 0.0));
    return (2.0 + 1.0 / roughness) * pow(sinTheta, 1.0 / roughness) / (TWO_PI);
}

vec3 evalSheen(vec3 sheenColor, float sheen, float roughness, float NdotL, float NdotV, float NdotH) {
    if (sheen <= 0.0) return vec3(0.0);
    float D = DCharlie(NdotH, roughness);
    float G = 1.0 / (1.0 + sheenLambda(NdotL, roughness) + sheenLambda(NdotV, roughness));
    return sheenColor * sheen * (D * G / max(4.0 * NdotL * NdotV, 1e-6));
}

#endif
