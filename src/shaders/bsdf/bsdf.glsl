#ifndef SHADER_BSDF_GLSL
#define SHADER_BSDF_GLSL

#include "core/types.glsl"
#include "core/math.glsl"
#include "utility/rand.glsl"

// Bounded VNDF Sampling for the Smith-GGX BRDF:
// https://gpuopen.com/download/Bounded_VNDF_Sampling_for_Smith-GGX_Reflections.pdf
// Sampling Visible GGX Normals with Spherical Caps:
// https://cdrdv2-public.intel.com/782052/sampling-visible-ggx-normals.pdf
// Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs:
// https://jcgt.org/published/0003/02/03/paper.pdf
// Practical multiple scattering compensation for microfacet models:
// https://blog.selfshadow.com/publications/turquin/ms_comp_final.pdf
// Enterprise PBR Shading Model:
// https://dassaultsystemes-technology.github.io/EnterprisePBRShadingModel/spec-2025x.md.html
// Generalization of Lambert's Reflectance Model:
// https://cave.cs.columbia.edu/Statics/publications/pdfs/Oren_SIGGRAPH94.pdf
// A tiny improvement of Oren-Nayar reflectance model:
// https://mimosa-pudica.net/improved-oren-nayar.html
// Production Friendly Microfacet Sheen BRDF:
// https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_sheen.pdf

struct BSDFSample {
    vec3 incoming;
    vec3 f;
    float pdf;
};

struct BSDFEval {
    vec3 f;
    float pdf;
};

struct SurfaceState {
    vec3 spec0;
    vec3 sheenColor;
    float diffuseWeight;
    float specularWeight;
    float clearcoatWeight;
    float sheenWeight;
    float diffuseRoughness;
    float ax;
    float ay;
    float clearcoatAlpha;
    float sheenRoughness;
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

float smithGGXCorrelatedVisibilityAniso(
    float NdotL,
    float NdotV,
    float LdotX,
    float LdotY,
    float VdotX,
    float VdotY,
    float ax,
    float ay
) {
    float lambdaV = NdotL * length(vec3(ax * VdotX, ay * VdotY, NdotV));
    float lambdaL = NdotV * length(vec3(ax * LdotX, ay * LdotY, NdotL));
    return 0.5 / max(lambdaV + lambdaL, 1e-6);
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

float boundedVNDFCapK(vec3 localV, float ax, float ay) {
    float a = min(min(ax, ay), 1.0);
    float s = 1.0 + length(localV.xy);
    float a2 = a * a;
    float s2 = s * s;
    return (1.0 - a2) * s2 / max(s2 + a2 * localV.z * localV.z, 1e-6);
}

float pdfGGXBoundedVNDF(vec3 normal, vec3 incoming, vec3 outgoing, float ax, float ay) {
    float NdotL = dot(normal, incoming);
    float NdotV = dot(normal, outgoing);
    if (NdotL <= 0.0 || NdotV <= 0.0) return 0.0;

    vec3 H = normalize(incoming + outgoing);
    float NdotH = max(dot(normal, H), 0.0);
    float LdotH = max(dot(incoming, H), 0.0);
    if (NdotH <= 0.0 || LdotH <= 0.0) return 0.0;

    mat3 basis = shadingBasis(normal);
    vec3 localV = vec3(dot(outgoing, basis[0]), dot(outgoing, basis[1]), NdotV);
    float HdotX = dot(H, basis[0]);
    float HdotY = dot(H, basis[1]);
    float Ds = GTR2Aniso(NdotH, HdotX, HdotY, ax, ay);

    vec2 ai = vec2(ax * localV.x, ay * localV.y);
    float len2 = dot(ai, ai);
    float t = sqrt(len2 + localV.z * localV.z);
    if (localV.z >= 0.0) {
        float k = boundedVNDFCapK(localV, ax, ay);
        return Ds / max(2.0 * (k * localV.z + t), 1e-6);
    }
    return Ds * (t - localV.z) / max(2.0 * len2, 1e-6);
}

vec3 sampleGGXBoundedVNDF(vec3 normal, vec3 outgoing, float ax, float ay, inout uint state) {
    mat3 basis = shadingBasis(normal);
    vec3 localV = vec3(dot(outgoing, basis[0]), dot(outgoing, basis[1]), dot(outgoing, basis[2]));
    vec3 stretchedV = normalize(vec3(localV.x * ax, localV.y * ay, localV.z));

    float phi = TWO_PI * rand(state);
    float k = boundedVNDFCapK(localV, ax, ay);
    float b = localV.z > 0.0 ? k * stretchedV.z : stretchedV.z;
    float z = 1.0 - rand(state) * (1.0 + b);
    float sinTheta = sqrt(max(1.0 - z * z, 0.0));
    vec3 localOStd = vec3(sinTheta * cos(phi), sinTheta * sin(phi), z);
    vec3 localMStd = stretchedV + localOStd;
    vec3 localM = normalize(vec3(localMStd.x * ax, localMStd.y * ay, localMStd.z));
    vec3 localL = reflect(-localV, localM);
    return normalize(basis * localL);
}

vec3 sampleClearcoatHalfVector(vec3 normal, float alpha, inout uint state) {
    float u1 = rand(state);
    float u2 = rand(state);
    float phi = TWO_PI * u2;
    float a2 = alpha * alpha;
    float cosTheta = sqrt(max((1.0 - pow(a2, 1.0 - u1)) / max(1.0 - a2, 1e-6), 0.0));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    mat3 basis = shadingBasis(normal);
    return normalize(
        basis[0] * (sinTheta * cos(phi)) +
            basis[1] * (sinTheta * sin(phi)) +
            basis[2] * cosTheta
    );
}

float ggxDirectionalAlbedoFit(float NdotV, float ax, float ay) {
    float alphaUV = clamp(ax * ay, 0.0, 1.0);
    float mu = clamp(NdotV, 0.0, 1.0);
    float viewPoly = 3.09507 + mu * (-9.11369 + mu * (15.8884 + mu * (-13.70343 + 4.51786 * mu)));
    float roughPoly = -0.20277 + alphaUV * (2.772 + alphaUV * (-2.6175 + 0.73343 * alphaUV));
    float Ess = 1.0 - 1.4594 * alphaUV * mu * roughPoly * viewPoly;
    return clamp(Ess, 1e-3, 1.0);
}

vec3 applyGGXMultiScatter(vec3 specular, vec3 spec0, float NdotV, float ax, float ay) {
    float Ess = ggxDirectionalAlbedoFit(NdotV, ax, ay);
    return specular * (1.0 + spec0 * (1.0 / Ess - 1.0));
}

float evalLegacySubsurface(float roughness, float NdotL, float NdotV, float LdotH) {
    float FL = schlickFresnel(NdotL);
    float FV = schlickFresnel(NdotV);
    float Fss90 = LdotH * LdotH * roughness;
    float Fss = mix(1.0, Fss90, FL) * mix(1.0, Fss90, FV);
    return 1.25 * (Fss * (1.0 / (NdotL + NdotV) - 0.5) + 0.5);
}

vec3 evalImprovedOrenNayar(vec3 albedo, float roughness, float NdotL, float NdotV, float LdotV) {
    float s = LdotV - NdotL * NdotV;
    float t = s <= 0.0 ? 1.0 : max(NdotL, NdotV);
    float denom = PI + (0.5 * PI - 2.0 / 3.0) * roughness;
    float A = 1.0 / denom;
    float B = roughness / denom;
    return albedo * (A + B * (s / t));
}

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

    SurfaceState surface = makeSurfaceState(material);
    mat3 basis = shadingBasis(normal);
    vec3 wiLocal = vec3(dot(incoming, basis[0]), dot(incoming, basis[1]), NdotL);
    vec3 woLocal = vec3(dot(outgoing, basis[0]), dot(outgoing, basis[1]), NdotV);
    float HdotX = dot(H, basis[0]);
    float HdotY = dot(H, basis[1]);
    float LdotX = wiLocal.x;
    float LdotY = wiLocal.y;
    float VdotX = woLocal.x;
    float VdotY = woLocal.y;

    float diffusePdf = NdotL * INV_PI;
    float sheenPdf = 0.5 * INV_PI;
    float Ds = GTR2Aniso(NdotH, HdotX, HdotY, surface.ax, surface.ay);
    float specularPdf = pdfGGXBoundedVNDF(normal, incoming, outgoing, surface.ax, surface.ay);
    float Dr = GTR1(NdotH, surface.clearcoatAlpha);
    float clearcoatPdf = Dr * NdotH / max(4.0 * LdotH, 1e-6);
    result.pdf = surface.diffuseWeight * diffusePdf
            + surface.sheenWeight * sheenPdf
            + surface.specularWeight * specularPdf
            + surface.clearcoatWeight * clearcoatPdf;

    vec3 roughDiffuse = evalImprovedOrenNayar(material.baseColor, surface.diffuseRoughness, NdotL, NdotV, dot(incoming, outgoing));
    float legacySubsurface = evalLegacySubsurface(surface.diffuseRoughness, NdotL, NdotV, LdotH);
    vec3 diffuse = mix(roughDiffuse, material.baseColor * (legacySubsurface * INV_PI), material.subsurface) * (1.0 - material.metallic);

    vec3 Fs = mix(surface.spec0, vec3(1.0), schlickFresnel(LdotH));
    float Gs = smithGGXCorrelatedVisibilityAniso(
            NdotL, NdotV, LdotX, LdotY, VdotX, VdotY, surface.ax, surface.ay);
    vec3 specular = applyGGXMultiScatter(Gs * Fs * Ds, surface.spec0, NdotV, surface.ax, surface.ay);

    vec3 sheen = evalSheen(surface.sheenColor, material.sheen * (1.0 - material.metallic), surface.sheenRoughness, NdotL, NdotV, NdotH);

    float Fr = mix(0.04, 1.0, schlickFresnel(LdotH));
    float Gr = smithGGX(NdotL, 0.25) * smithGGX(NdotV, 0.25);
    vec3 clearcoat = vec3(0.25 * material.clearcoat * Gr * Fr * Dr);

    result.f = diffuse + sheen + specular + clearcoat;
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

    SurfaceState surface = makeSurfaceState(material);
    float lobe = rand(state);
    if (lobe < surface.diffuseWeight || (surface.specularWeight <= 0.0 && surface.clearcoatWeight <= 0.0 && surface.sheenWeight <= 0.0)) {
        result.incoming = sampleCosineHemisphere(normal, state);
    } else if (lobe < surface.diffuseWeight + surface.sheenWeight || (surface.specularWeight <= 0.0 && surface.clearcoatWeight <= 0.0)) {
        result.incoming = shadingBasis(normal) * sampleUniformHemisphereLocal(rand(state), rand(state));
    } else if (lobe < surface.diffuseWeight + surface.sheenWeight + surface.specularWeight || surface.clearcoatWeight <= 0.0) {
        for (int attempt = 0; attempt < 8; attempt++) {
            vec3 incoming = sampleGGXBoundedVNDF(normal, outgoing, surface.ax, surface.ay, state);
            if (dot(normal, incoming) > 0.0) {
                result.incoming = incoming;
                break;
            }
        }
    } else {
        for (int attempt = 0; attempt < 8; attempt++) {
            vec3 H = sampleClearcoatHalfVector(normal, surface.clearcoatAlpha, state);
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
