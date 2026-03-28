#ifndef VKRT_UTILITY_RGB2SPEC_SLANG
#define VKRT_UTILITY_RGB2SPEC_SLANG

#include "../../shared/types.h"

// References:
// Jakob, Hanika, 2019 - https://rgl.epfl.ch/publications/Jakob2019Spectral
// Supplemental runtime implementation: https://github.com/mitsuba-renderer/rgb2spec/blob/master/rgb2spec.c
// Supplemental API definition: https://github.com/mitsuba-renderer/rgb2spec/blob/master/rgb2spec.h

static const uint VKRT_RGB2SPEC_N_COEFFS = 3u;
static const float VKRT_RGB2SPEC_EPSILON = 1e-8;

struct RGB2SpecModel {
    uint res;
    uint scaleOffset;
    uint dataOffset;
};

RGB2SpecModel rgb2specModelFromTableInfo(RGB2SpecTableInfo info) {
    RGB2SpecModel model;
    model.res = info.res;
    model.scaleOffset = info.scaleOffset;
    model.dataOffset = info.dataOffset;
    return model;
}

bool rgb2specTableInfoIsValid(RGB2SpecTableInfo info) {
    return info.valid != 0u && info.res > 1u;
}

float rgb2specScaleValue(RGB2SpecModel model, StructuredBuffer<float> table, uint index) {
    return table[model.scaleOffset + index];
}

float rgb2specDataValue(RGB2SpecModel model, StructuredBuffer<float> table, uint index) {
    return table[model.dataOffset + index];
}

float rgb2specMaxComponent(float3 rgb) {
    return max(rgb.x, max(rgb.y, rgb.z));
}

uint rgb2specDominantChannel(float3 rgb) {
    uint dominantChannel = 0u;
    [unroll]
    for (uint channel = 1u; channel < 3u; ++channel) {
        if (rgb[channel] >= rgb[dominantChannel]) {
            dominantChannel = channel;
        }
    }
    return dominantChannel;
}

uint rgb2specFindInterval(RGB2SpecModel model, StructuredBuffer<float> table, float x) {
    int left = 0;
    int lastInterval = int(model.res) - 2;
    int size = lastInterval;

    while (size > 0) {
        int half = size >> 1;
        int middle = left + half + 1;

        if (rgb2specScaleValue(model, table, uint(middle)) < x) {
            left = middle;
            size -= half + 1;
        } else {
            size = half;
        }
    }

    return min(uint(left), model.res - 2u);
}

float3 rgb2specFetch(RGB2SpecModel model, StructuredBuffer<float> table, float3 rgb) {
    float3 safeRgb = max(rgb, float3(0.0));
    float z = rgb2specMaxComponent(safeRgb);
    if (z <= VKRT_RGB2SPEC_EPSILON) {
        return float3(0.0);
    }

    uint dominantChannel = rgb2specDominantChannel(safeRgb);
    float scale = float(model.res - 1u) / z;
    float x = safeRgb[(dominantChannel + 1u) % 3u] * scale;
    float y = safeRgb[(dominantChannel + 2u) % 3u] * scale;

    uint xi = min(uint(x), model.res - 2u);
    uint yi = min(uint(y), model.res - 2u);
    uint zi = rgb2specFindInterval(model, table, z);

    uint offset = ((((dominantChannel * model.res + zi) * model.res + yi) * model.res + xi) * VKRT_RGB2SPEC_N_COEFFS);
    uint dx = VKRT_RGB2SPEC_N_COEFFS;
    uint dy = VKRT_RGB2SPEC_N_COEFFS * model.res;
    uint dz = VKRT_RGB2SPEC_N_COEFFS * model.res * model.res;

    float x1 = x - float(xi);
    float x0 = 1.0 - x1;
    float y1 = y - float(yi);
    float y0 = 1.0 - y1;

    float scale0 = rgb2specScaleValue(model, table, zi);
    float scale1 = rgb2specScaleValue(model, table, zi + 1u);
    float zDenom = max(scale1 - scale0, VKRT_RGB2SPEC_EPSILON);
    float z1 = (z - scale0) / zDenom;
    float z0 = 1.0 - z1;

    float3 coeff = float3(0.0);
    [unroll]
    for (uint j = 0; j < VKRT_RGB2SPEC_N_COEFFS; ++j) {
        uint coeffOffset = offset + j;
        coeff[j] = ((rgb2specDataValue(model, table, coeffOffset) * x0 +
                     rgb2specDataValue(model, table, coeffOffset + dx) * x1) *
                        y0 +
                    (rgb2specDataValue(model, table, coeffOffset + dy) * x0 +
                     rgb2specDataValue(model, table, coeffOffset + dy + dx) * x1) *
                        y1) *
                       z0 +
                   ((rgb2specDataValue(model, table, coeffOffset + dz) * x0 +
                     rgb2specDataValue(model, table, coeffOffset + dz + dx) * x1) *
                        y0 +
                    (rgb2specDataValue(model, table, coeffOffset + dz + dy) * x0 +
                     rgb2specDataValue(model, table, coeffOffset + dz + dy + dx) * x1) *
                        y1) *
                       z1;
    }

    return coeff;
}

float rgb2specEvalPrecise(float3 coeff, float lambdaNm) {
    float x = (coeff.x * lambdaNm + coeff.y) * lambdaNm + coeff.z;
    float y = 1.0 / sqrt(x * x + 1.0);
    return 0.5 * x * y + 0.5;
}

float rgb2specEvalFast(float3 coeff, float lambdaNm) {
    float x = (coeff.x * lambdaNm + coeff.y) * lambdaNm + coeff.z;
    float y = rsqrt(x * x + 1.0);
    return 0.5 * x * y + 0.5;
}

float rgb2specEvalPrecise(RGB2SpecModel model, StructuredBuffer<float> table, float3 rgb, float lambdaNm) {
    return rgb2specEvalPrecise(rgb2specFetch(model, table, rgb), lambdaNm);
}

float rgb2specEvalFast(RGB2SpecModel model, StructuredBuffer<float> table, float3 rgb, float lambdaNm) {
    return rgb2specEvalFast(rgb2specFetch(model, table, rgb), lambdaNm);
}

#endif
