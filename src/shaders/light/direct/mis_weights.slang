#ifndef VKRT_LIGHT_DIRECT_MIS_WEIGHTS_SLANG
#define VKRT_LIGHT_DIRECT_MIS_WEIGHTS_SLANG

#include "../../sampling/discrete.slang"

float lightPdfAreaToSolidAngle(float lightPdfArea, float3 geometricNormal, float3 rayDirection, float hitDistance) {
    if (lightPdfArea <= 0.0) return 0.0;

    float distanceSquared = hitDistance * hitDistance;
    if (distanceSquared <= 0.0) return 0.0;

    float cosLight = abs(dot(rayDirection, geometricNormal));
    if (cosLight <= 0.0) return 0.0;

    return lightPdfArea * distanceSquared / cosLight;
}

float computeBSDFEmitterMISWeight(
    float bsdfPdf,
    float lightPdfArea,
    float3 geometricNormal,
    float3 rayDirection,
    float hitDistance
) {
    float lightPdfSolidAngle = lightPdfAreaToSolidAngle(lightPdfArea, geometricNormal, rayDirection, hitDistance);
    return lightPdfSolidAngle <= 0.0 ? 1.0 : powerHeuristic(bsdfPdf, lightPdfSolidAngle);
}

float computeSpectralMISWeight(float4 sampledTechniquePdf, float4 alternateTechniquePdf) {
    float denominator = dot(sampledTechniquePdf, float4(1.0)) + dot(alternateTechniquePdf, float4(1.0));
    return denominator > 0.0 ? sampledTechniquePdf.x / denominator : 0.0;
}

float computeSpectralEmitterMISWeight(
    float4 previousVertexTechniquePathPdf,
    float4 previousBsdfTechniquePdf,
    float lightPdfSolidAngle
) {
    float4 bsdfTechniquePdf = previousVertexTechniquePathPdf * previousBsdfTechniquePdf;
    float4 lightTechniquePdf = previousVertexTechniquePathPdf * lightPdfSolidAngle;
    return computeSpectralMISWeight(bsdfTechniquePdf, lightTechniquePdf);
}

#endif
