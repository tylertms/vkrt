#ifndef SHADER_BSDF_GGX_GLSL
#define SHADER_BSDF_GGX_GLSL

#include "core/math.glsl"
#include "utility/rand.glsl"

float D_GGX(float cosTheta, float alpha) {
    float alpha2 = alpha * alpha;
    float denom = cosTheta * cosTheta * (alpha2 - 1.0) + 1.0;
    return alpha2 * INV_PI / (denom * denom);
}

float G1_Smith(float cosTheta, float alpha) {
    float k = alpha * 0.5;
    return cosTheta / (cosTheta * (1.0 - k) + k);
}

float G_Smith(float cosThetaL, float cosThetaV, float alpha) {
    return G1_Smith(cosThetaL, alpha) * G1_Smith(cosThetaV, alpha);
}

vec3 F_Schlick(float cosTheta, vec3 f0) {
    float t = 1.0 - cosTheta;
    float t2 = t * t;
    return f0 + (1.0 - f0) * (t2 * t2 * t);
}

vec3 sampleGGX(vec3 normal, vec3 outgoing, float alpha, inout uint state) {
    float r1 = rand(state);
    float r2 = rand(state);

    float theta = atan(alpha * sqrt(r1), sqrt(1.0 - r1));
    float phi = 2.0 * PI * r2;

    float sinTheta = sin(theta);
    vec3 halfLocal = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cos(theta));

    mat3 tbn = buildTBN(normal);
    vec3 halfVector = tbn * halfLocal;

    return reflect(-outgoing, halfVector);
}

vec3 evalGGX(vec3 normal, vec3 incoming, vec3 outgoing, float alpha, vec3 f0) {
    vec3 halfVector = normalize(incoming + outgoing);

    float cosNH = max(dot(normal, halfVector), 0.0);
    float cosNL = max(dot(normal, incoming), 0.0);
    float cosNV = max(dot(normal, outgoing), 0.0);
    float cosVH = max(dot(outgoing, halfVector), 0.0);

    if (cosNL < 1e-7 || cosNV < 1e-7) return vec3(0.0);

    float D = D_GGX(cosNH, alpha);
    float G = G_Smith(cosNL, cosNV, alpha);
    vec3 F = F_Schlick(cosVH, f0);

    return D * G * F / (4.0 * cosNL * cosNV);
}

float pdfGGX(vec3 normal, vec3 incoming, vec3 outgoing, float alpha) {
    vec3 halfVector = normalize(incoming + outgoing);

    float cosNH = max(dot(normal, halfVector), 0.0);
    float cosVH = max(dot(outgoing, halfVector), 0.0);

    if (cosVH < 1e-7) return 0.0;

    return D_GGX(cosNH, alpha) * cosNH / (4.0 * cosVH);
}

#endif
