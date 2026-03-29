#ifndef VKRT_UTILITY_RGB2SPEC_SLANG
#define VKRT_UTILITY_RGB2SPEC_SLANG

#include "../../shared/types.h"

// References:
// Jakob, Hanika, 2019 - https://rgl.epfl.ch/publications/Jakob2019Spectral
// Supplemental runtime implementation: https://github.com/mitsuba-renderer/rgb2spec/blob/master/rgb2spec.c
// Supplemental API definition: https://github.com/mitsuba-renderer/rgb2spec/blob/master/rgb2spec.h

static const uint VKRT_RGB2SPEC_N_COEFFS = 3u;
static const float VKRT_RGB2SPEC_EPSILON = 1e-8;

uint rgb2specFindInterval(RGB2SpecTableInfo info, StructuredBuffer<float> table, float x) {
    int left = 0;
    int size = int(info.res) - 2;

    while (size > 0) {
        int half = size >> 1;
        int middle = left + half + 1;

        if (table[info.scaleOffset + uint(middle)] <= x) {
            left = middle;
            size -= half + 1;
        } else {
            size = half;
        }
    }

    return min(uint(left), info.res - 2u);
}

float3 rgb2specFetch(RGB2SpecTableInfo info, StructuredBuffer<float> table, float3 rgb) {
    float z = max(rgb.x, max(rgb.y, rgb.z));
    if (z <= VKRT_RGB2SPEC_EPSILON) {
        return float3(0.0);
    }

    uint dominantChannel = 0u;
    [unroll] for (uint channel = 1u; channel < 3u; ++channel) {
        if (rgb[channel] >= rgb[dominantChannel]) {
            dominantChannel = channel;
        }
    }

    float xyScale = float(info.res - 1u) / z;
    float x = rgb[(dominantChannel + 1u) % 3u] * xyScale;
    float y = rgb[(dominantChannel + 2u) % 3u] * xyScale;

    uint xi = min(uint(x), info.res - 2u);
    uint yi = min(uint(y), info.res - 2u);
    uint zi = rgb2specFindInterval(info, table, z);

    uint offset = ((((dominantChannel * info.res + zi) * info.res + yi) * info.res + xi) * VKRT_RGB2SPEC_N_COEFFS);
    uint dx = VKRT_RGB2SPEC_N_COEFFS;
    uint dy = dx * info.res;
    uint dz = dy * info.res;

    float x1 = x - float(xi);
    float y1 = y - float(yi);
    float x0 = 1.0 - x1;
    float y0 = 1.0 - y1;

    float scale0 = table[info.scaleOffset + zi];
    float scale1 = table[info.scaleOffset + zi + 1u];
    float z1 = (z - scale0) / max(scale1 - scale0, VKRT_RGB2SPEC_EPSILON);
    float z0 = 1.0 - z1;

    float3 coeff = float3(0.0);
    [unroll] for (uint j = 0; j < VKRT_RGB2SPEC_N_COEFFS; ++j) {
        uint coeffOffset = info.dataOffset + offset + j;
        coeff[j] = ((table[coeffOffset] * x0 + table[coeffOffset + dx] * x1) * y0 +
                    (table[coeffOffset + dy] * x0 + table[coeffOffset + dy + dx] * x1) * y1) *
                       z0 +
                   ((table[coeffOffset + dz] * x0 + table[coeffOffset + dz + dx] * x1) * y0 +
                    (table[coeffOffset + dz + dy] * x0 + table[coeffOffset + dz + dy + dx] * x1) * y1) *
                       z1;
    }

    return coeff;
}

float rgb2specEval(RGB2SpecTableInfo info, StructuredBuffer<float> table, float3 rgb, float lambdaNm) {
    float3 coeff = rgb2specFetch(info, table, rgb);
    float x = (coeff.x * lambdaNm + coeff.y) * lambdaNm + coeff.z;
    return 0.5 * x * rsqrt(x * x + 1.0) + 0.5;
}

#endif
