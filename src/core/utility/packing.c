#include "packing.h"

#include "types.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static float saturatef(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float clampf(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static void normalize3f(const float input[3], float output[3]) {
    float lengthSq = (input[0] * input[0]) + (input[1] * input[1]) + (input[2] * input[2]);
    if (lengthSq <= 1e-20f) {
        output[0] = 0.0f;
        output[1] = 0.0f;
        output[2] = 1.0f;
        return;
    }

    float invLength = 1.0f / sqrtf(lengthSq);
    output[0] = input[0] * invLength;
    output[1] = input[1] * invLength;
    output[2] = input[2] * invLength;
}

static uint16_t f32tof16(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));

    uint32_t sign = (bits >> 16u) & 0x8000u;
    uint32_t mantissa = bits & 0x007fffffu;
    int32_t exponent = (int32_t)((bits >> 23u) & 0xffu) - 127;

    if (exponent == 128) {
        if (mantissa != 0) {
            uint16_t nanBits = (uint16_t)(sign | 0x7c00u | (mantissa >> 13u));
            if ((nanBits & 0x03ffu) == 0) nanBits |= 1u;
            return nanBits;
        }
        return (uint16_t)(sign | 0x7c00u);
    }

    if (exponent > 15) {
        return (uint16_t)(sign | 0x7c00u);
    }

    if (exponent >= -14) {
        uint32_t expBits = (uint32_t)(exponent + 15) << 10u;
        uint32_t mantBits = mantissa >> 13u;
        uint32_t roundBit = (mantissa >> 12u) & 1u;
        uint32_t sticky = mantissa & 0x00000fffu;

        uint32_t half = sign | expBits | mantBits;
        if (roundBit && (sticky || (half & 1u))) {
            half++;
        }
        return (uint16_t)half;
    }

    if (exponent < -24) {
        return (uint16_t)sign;
    }

    mantissa |= 0x00800000u;
    uint32_t shift = (uint32_t)(-exponent - 1);
    uint32_t mantBits = mantissa >> (shift + 13u);
    uint32_t roundBit = (mantissa >> (shift + 12u)) & 1u;
    uint32_t stickyMask = (1u << (shift + 12u)) - 1u;
    uint32_t sticky = mantissa & stickyMask;

    uint32_t half = sign | mantBits;
    if (roundBit && (sticky || (half & 1u))) {
        half++;
    }
    return (uint16_t)half;
}

uint32_t packHalf2(const float input[2]) {
    return (uint32_t)f32tof16(input[0]) | ((uint32_t)f32tof16(input[1]) << sizeof(uint16_t));
}

uint32_t packOctNormal32(const float normal[3]) {
    float norm[3];
    normalize3f(normal, norm);
    float invL1 = 1.0f / (fabsf(norm[0]) + fabsf(norm[1]) + fabsf(norm[2]));
    float projX = norm[0] * invL1;
    float projY = norm[1] * invL1;

    if (norm[2] < 0.0f) {
        float oldX = projX;
        projX = (1.0f - fabsf(projY)) * (oldX >= 0.0f ? 1.0f : -1.0f);
        projY = (1.0f - fabsf(oldX)) * (projY >= 0.0f ? 1.0f : -1.0f);
    }

    int32_t signedProjX = (int32_t)lroundf(clampf(projX, -1.0f, 1.0f) * 32767.0f);
    int32_t signedProjY = (int32_t)lroundf(clampf(projY, -1.0f, 1.0f) * 32767.0f);
    return ((uint32_t)signedProjX & 0xffffu) | (((uint32_t)signedProjY & 0xffffu) << 16u);
}

uint32_t packSnorm15(float value) {
    int32_t scaled = (int32_t)lroundf(clampf(value, -1.0f, 1.0f) * 16383.0f);
    return (uint32_t)scaled & 0x7fffu;
}

uint32_t packTangent32(const float tangent[4]) {
    float tangent3[3] = {tangent[0], tangent[1], tangent[2]};
    float norm[3];
    normalize3f(tangent3, norm);
    float invL1 = 1.0f / (fabsf(norm[0]) + fabsf(norm[1]) + fabsf(norm[2]));
    float projX = norm[0] * invL1;
    float projY = norm[1] * invL1;

    if (norm[2] < 0.0f) {
        float oldX = projX;
        projX = (1.0f - fabsf(projY)) * (oldX >= 0.0f ? 1.0f : -1.0f);
        projY = (1.0f - fabsf(oldX)) * (projY >= 0.0f ? 1.0f : -1.0f);
    }

    uint32_t packed = packSnorm15(projX) | (packSnorm15(projY) << 15u);
    if (tangent[3] < 0.0f) {
        packed |= 0x80000000u;
    }
    return packed;
}

uint32_t packColorRGBA8(const float color[4]) {
    uint32_t r = (uint32_t)lroundf(saturatef(color[0]) * 255.0f);
    uint32_t g = (uint32_t)lroundf(saturatef(color[1]) * 255.0f);
    uint32_t b = (uint32_t)lroundf(saturatef(color[2]) * 255.0f);
    uint32_t a = (uint32_t)lroundf(saturatef(color[3]) * 255.0f);
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

ShaderVertex packShaderVertex(const Vertex* vertex) {
    ShaderVertex packed = {0};
    if (!vertex) return packed;

    memcpy(packed.position, vertex->position, sizeof(packed.position));
    packed.packedNormal = packOctNormal32(vertex->normal);
    packed.packedTangent = packTangent32(vertex->tangent);
    packed.packedColor = packColorRGBA8(vertex->color);
    memcpy(packed.texcoord0, vertex->texcoord0, sizeof(packed.texcoord0));
    memcpy(packed.texcoord1, vertex->texcoord1, sizeof(packed.texcoord1));

    return packed;
}
