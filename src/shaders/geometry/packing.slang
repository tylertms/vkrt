#ifndef VKRT_UTILITY_PACKING_SLANG
#define VKRT_UTILITY_PACKING_SLANG

static const uint VKRT_PACKED_UNORM8_MASK = 0xffu;
static const float VKRT_PACKED_UNORM8_SCALE_INV = 1.0 / 255.0;

static const float VKRT_PACKED_OCT_SNORM16_SCALE = 32767.0;

static const uint VKRT_PACKED_TANGENT_COMPONENT_MASK = 0x7fffu;
static const uint VKRT_PACKED_TANGENT_SIGN_BIT = 0x80000000u;
static const float VKRT_PACKED_TANGENT_SNORM15_SCALE = 16383.0;

float3 decodeOct(float2 p) {
    float z = 1.0 - abs(p.x) - abs(p.y);
    if (z < 0.0) {
        float oldX = p.x;
        p.x = (1.0 - abs(p.y)) * (oldX >= 0.0 ? 1.0 : -1.0);
        p.y = (1.0 - abs(oldX)) * (p.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(float3(p.x, p.y, z));
}

float3 unpackOctNormal(uint packed) {
    float2 p = float2(
        float(int(packed << 16) >> 16) / VKRT_PACKED_OCT_SNORM16_SCALE,
        float(int(packed) >> 16) / VKRT_PACKED_OCT_SNORM16_SCALE
    );
    return decodeOct(p);
}

float unpackUnorm8(uint value) {
    return float(value & VKRT_PACKED_UNORM8_MASK) * VKRT_PACKED_UNORM8_SCALE_INV;
}

float4 unpackColorRGBA8(uint packed) {
    return float4(
        unpackUnorm8(packed),
        unpackUnorm8(packed >> 8u),
        unpackUnorm8(packed >> 16u),
        unpackUnorm8(packed >> 24u)
    );
}

float unpackSnorm15(uint value) {
    int signedValue = int(value & VKRT_PACKED_TANGENT_COMPONENT_MASK);
    if ((signedValue & 0x4000) != 0) {
        signedValue |= ~int(VKRT_PACKED_TANGENT_COMPONENT_MASK);
    }
    return clamp(float(signedValue) / VKRT_PACKED_TANGENT_SNORM15_SCALE, -1.0, 1.0);
}

float4 unpackOctTangent(uint packed) {
    float2 p = float2(unpackSnorm15(packed), unpackSnorm15(packed >> 15u));
    float handedness = (packed & VKRT_PACKED_TANGENT_SIGN_BIT) != 0u ? -1.0 : 1.0;
    return float4(decodeOct(p), handedness);
}

float2 unpackHalf2(uint packed) {
    return unpackHalf2x16ToFloat(packed);
}

#endif
