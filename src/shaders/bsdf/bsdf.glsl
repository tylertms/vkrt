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
// EON: A Practical Energy-Preserving Rough Diffuse BRDF:
// https://jcgt.org/published/0014/01/06/paper.pdf
// Production Friendly Microfacet Sheen BRDF:
// https://blog.selfshadow.com/publications/s2017-shading-course/imageworks/s2017_pbs_imageworks_sheen.pdf

#include "bsdf/types.glsl"
#include "bsdf/common.glsl"
#include "bsdf/specular.glsl"
#include "bsdf/diffuse.glsl"
#include "bsdf/sheen.glsl"

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

    vec3 diffuse = evalEONDiffuse(
            material.baseColor, surface.diffuseRoughness, NdotL, NdotV, dot(incoming, outgoing))
            * (1.0 - material.metallic);

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
