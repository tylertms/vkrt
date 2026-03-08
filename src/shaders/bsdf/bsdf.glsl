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

struct BSDFEval {
    vec3 f;
    float pdf;
};

struct DisneyState {
    vec3 spec0;
    vec3 sheenColor;
    float diffuseWeight;
    float specularWeight;
    float ax;
    float ay;
    float clearcoatAlpha;
};

float sqr(float x) {
    return x * x;
}

float schlickFresnel(float u) {
    float m = clamp(1.0 - u, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}

float GTR1(float NdotH, float a) {
    if (a >= 1.0) return INV_PI;
    float a2 = a * a;
    float t = 1.0 + (a2 - 1.0) * NdotH * NdotH;
    return (a2 - 1.0) / (PI * log(a2) * t);
}

float GTR2Aniso(float NdotH, float HdotX, float HdotY, float ax, float ay) {
    float sx = HdotX / ax;
    float sy = HdotY / ay;
    float denom = sx * sx + sy * sy + NdotH * NdotH;
    return 1.0 / (PI * ax * ay * denom * denom);
}

float smithGGX(float NdotV, float alphaG) {
    float a = alphaG * alphaG;
    float b = NdotV * NdotV;
    return 1.0 / (NdotV + sqrt(max(a + b - a * b, 0.0)));
}

float smithGGXAniso(float NdotV, float VdotX, float VdotY, float ax, float ay) {
    float sx = VdotX * ax;
    float sy = VdotY * ay;
    return 1.0 / (NdotV + sqrt(sx * sx + sy * sy + NdotV * NdotV));
}

mat3 disneyBasis(vec3 normal) {
    return buildTBN(normal);
}

DisneyState disneyState(Material material) {
    DisneyState state;

    float baseLum = luminance(material.baseColor);
    vec3 tint = baseLum > 0.0 ? material.baseColor / baseLum : vec3(1.0);

    vec3 dielectric = material.specular * 0.08 * mix(vec3(1.0), tint, material.specularTint);
    state.spec0 = mix(dielectric, material.baseColor, material.metallic);
    state.sheenColor = mix(vec3(1.0), tint, material.sheenTint);

    state.diffuseWeight = (1.0 - material.metallic) * (baseLum + 0.5 * material.sheen);

    float glossyBoost = mix(8.0, 1.0, material.roughness);
    state.specularWeight = (luminance(state.spec0) + 0.25 * material.clearcoat) * glossyBoost;

    float sum = state.diffuseWeight + state.specularWeight;
    if (sum <= 1e-6) {
        state.diffuseWeight = 1.0;
        state.specularWeight = 0.0;
    } else {
        float invSum = 1.0 / sum;
        state.diffuseWeight *= invSum;
        state.specularWeight *= invSum;
    }

    float roughness = max(material.roughness, 0.001);
    float roughness2 = roughness * roughness;
    float aspect = sqrt(max(0.1, 1.0 - material.anisotropic * 0.9));
    state.ax = max(0.001, roughness2 / aspect);
    state.ay = max(0.001, roughness2 * aspect);
    state.clearcoatAlpha = mix(0.1, 0.001, material.clearcoatGloss);
    return state;
}

vec3 fresnelSchlick(float cosTheta, Material material) {
    return mix(disneyState(material).spec0, vec3(1.0), schlickFresnel(cosTheta));
}

vec3 sampleCosineHemisphere(vec3 normal, inout uint state) {
    float r1 = rand(state);
    float r2 = rand(state);
    float r = sqrt(r1);
    float phi = TWO_PI * r2;

    vec3 local = vec3(r * cos(phi), r * sin(phi), sqrt(max(1.0 - r1, 0.0)));
    return disneyBasis(normal) * local;
}

vec3 sampleAnisotropicGGXHalfVector(vec3 normal, float ax, float ay, inout uint state) {
    float u1 = rand(state);
    float u2 = rand(state);

    float phi = atan(ay * sin(TWO_PI * u2), ax * cos(TWO_PI * u2));
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);
    float invAlpha2 = sqr(cosPhi / ax) + sqr(sinPhi / ay);
    float alpha2 = 1.0 / max(invAlpha2, 1e-6);
    float tanTheta2 = alpha2 * u1 / max(1.0 - u1, 1e-6);
    float cosTheta = inversesqrt(1.0 + tanTheta2);
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));

    mat3 basis = disneyBasis(normal);
    return normalize(
        basis[0] * (sinTheta * cosPhi) +
            basis[1] * (sinTheta * sinPhi) +
            basis[2] * cosTheta
    );
}

BSDFEval evalBSDFAll(vec3 normal, vec3 incoming, vec3 outgoing, Material material) {
    BSDFEval result;
    result.f = vec3(0.0);
    result.pdf = 0.0;

    float NdotL = dot(normal, incoming);
    float NdotV = dot(normal, outgoing);
    if (NdotL <= 0.0 || NdotV <= 0.0) return result;

    vec3 H = normalize(incoming + outgoing);
    float NdotH = max(dot(normal, H), 0.0);
    float LdotH = max(dot(incoming, H), 0.0);
    if (NdotH <= 0.0 || LdotH <= 0.0) return result;

    DisneyState state = disneyState(material);
    mat3 basis = disneyBasis(normal);
    float HdotX = dot(H, basis[0]);
    float HdotY = dot(H, basis[1]);
    float LdotX = dot(incoming, basis[0]);
    float LdotY = dot(incoming, basis[1]);
    float VdotX = dot(outgoing, basis[0]);
    float VdotY = dot(outgoing, basis[1]);

    float Ds = GTR2Aniso(NdotH, HdotX, HdotY, state.ax, state.ay);
    result.pdf = state.diffuseWeight * (NdotL * INV_PI)
            + state.specularWeight * (Ds * NdotH / (4.0 * LdotH));

    float FL = schlickFresnel(NdotL);
    float FV = schlickFresnel(NdotV);
    float FH = schlickFresnel(LdotH);

    float Fd90 = 0.5 + 2.0 * LdotH * LdotH * material.roughness;
    float Fd = mix(1.0, Fd90, FL) * mix(1.0, Fd90, FV);

    float Fss90 = LdotH * LdotH * material.roughness;
    float Fss = mix(1.0, Fss90, FL) * mix(1.0, Fss90, FV);
    float ss = 1.25 * (Fss * (1.0 / (NdotL + NdotV) - 0.5) + 0.5);

    vec3 diffuse = (INV_PI * mix(Fd, ss, material.subsurface) * material.baseColor
            + FH * material.sheen * state.sheenColor) * (1.0 - material.metallic);

    vec3 Fs = mix(state.spec0, vec3(1.0), FH);
    float Gs = smithGGXAniso(NdotL, LdotX, LdotY, state.ax, state.ay)
            * smithGGXAniso(NdotV, VdotX, VdotY, state.ax, state.ay);
    vec3 specular = Gs * Fs * Ds;

    float Dr = GTR1(NdotH, state.clearcoatAlpha);
    float Fr = mix(0.04, 1.0, FH);
    float Gr = smithGGX(NdotL, 0.25) * smithGGX(NdotV, 0.25);
    vec3 clearcoat = vec3(0.25 * material.clearcoat * Gr * Fr * Dr);

    result.f = diffuse + specular + clearcoat;
    return result;
}

vec3 evalBSDF(vec3 normal, vec3 incoming, vec3 outgoing, Material material) {
    return evalBSDFAll(normal, incoming, outgoing, material).f;
}

float pdfBSDF(vec3 normal, vec3 incoming, vec3 outgoing, Material material) {
    return evalBSDFAll(normal, incoming, outgoing, material).pdf;
}

BSDFSample sampleBSDF(vec3 normal, vec3 outgoing, Material material, inout uint state) {
    BSDFSample result;
    result.incoming = vec3(0.0);
    result.f = vec3(0.0);
    result.pdf = 0.0;

    DisneyState disney = disneyState(material);
    if (rand(state) < disney.diffuseWeight || disney.specularWeight <= 0.0) {
        result.incoming = sampleCosineHemisphere(normal, state);
    } else {
        for (int attempt = 0; attempt < 16; attempt++) {
            vec3 H = sampleAnisotropicGGXHalfVector(normal, disney.ax, disney.ay, state);
            vec3 incoming = reflect(-outgoing, H);
            if (dot(normal, incoming) > 0.0 && dot(outgoing, H) > 0.0) {
                result.incoming = incoming;
                break;
            }
        }
    }

    BSDFEval eval = evalBSDFAll(normal, result.incoming, outgoing, material);
    result.f = eval.f;
    result.pdf = eval.pdf;
    return result;
}

#endif
