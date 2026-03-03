#ifndef SHADER_BSDF_GGX_GLSL
#define SHADER_BSDF_GGX_GLSL

#include "core/math.glsl"
#include "utility/rand.glsl"

float D_GGX(float cosTheta, float alpha) {
    if (cosTheta <= 0.0) return 0.0;
    float alpha2 = alpha * alpha;
    float denom = cosTheta * cosTheta * (alpha2 - 1.0) + 1.0;
    return alpha2 * INV_PI / (denom * denom);
}

float G1_Smith(float cosTheta, float alpha) {
    if (cosTheta <= 0.0) return 0.0;
    float alpha2 = alpha * alpha;
    float cos2 = cosTheta * cosTheta;
    float denom = cosTheta + sqrt(alpha2 + (1.0 - alpha2) * cos2);
    return (2.0 * cosTheta) / max(denom, 1e-7);
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

    float alpha2 = alpha * alpha;
    float cosTheta2 = (1.0 - r1) / (1.0 + (alpha2 - 1.0) * r1);
    float cosTheta = sqrt(max(cosTheta2, 0.0));
    float sinTheta = sqrt(max(1.0 - cosTheta2, 0.0));
    float phi = 2.0 * PI * r2;

    vec3 halfLocal = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    mat3 tbn = buildTBN(normal);
    vec3 halfVector = normalize(tbn * halfLocal);

    return reflect(-outgoing, halfVector);
}

vec3 evalGGX(vec3 normal, vec3 incoming, vec3 outgoing, float alpha, vec3 f0) {
    vec3 halfVectorUnnormalized = incoming + outgoing;
    float halfLengthSq = dot(halfVectorUnnormalized, halfVectorUnnormalized);
    if (halfLengthSq <= 1e-8) return vec3(0.0);
    vec3 halfVector = halfVectorUnnormalized * inversesqrt(halfLengthSq);

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
    float cosNL = max(dot(normal, incoming), 0.0);
    float cosNV = max(dot(normal, outgoing), 0.0);
    if (cosNL <= 0.0 || cosNV <= 0.0) return 0.0;

    vec3 h = incoming + outgoing;
    float hLenSq = dot(h, h);
    if (hLenSq <= 1e-8) return 0.0;
    h *= inversesqrt(hLenSq);

    float cosNH = max(dot(normal, h), 0.0);
    float cosVH = max(dot(outgoing, h), 0.0);
    if (cosVH <= 1e-7) return 0.0;

    return D_GGX(cosNH, alpha) * cosNH / (4.0 * cosVH);
}

#endif
