#ifndef VKRT_BSDF_BASE_MATH_SLANG
#define VKRT_BSDF_BASE_MATH_SLANG

static const float VKRT_PI = 3.14159265358979323846;
static const float VKRT_INV_PI = 0.31830988618379067154;

float pow5(float value) {
    float value2 = value * value;
    return value2 * value2 * value;
}

float schlickWeight(float cosTheta) {
    return pow5(1.0 - saturate(cosTheta));
}

float cosTheta(float3 w) {
    return w.z;
}

float absCosTheta(float3 w) {
    return abs(w.z);
}

float cos2Theta(float3 w) {
    return w.z * w.z;
}

float sin2Theta(float3 w) {
    return saturate(1.0 - cos2Theta(w));
}

float sinTheta(float3 w) {
    return sqrt(sin2Theta(w));
}

float sinPhi(float3 w) {
    float sinTheta2 = sin2Theta(w);
    return sinTheta2 > 0.0 ? clamp(w.y * rsqrt(sinTheta2), -1.0, 1.0) : 0.0;
}

float cosPhi(float3 w) {
    float sinTheta2 = sin2Theta(w);
    return sinTheta2 > 0.0 ? clamp(w.x * rsqrt(sinTheta2), -1.0, 1.0) : 1.0;
}

float cosPhiDifference(float3 wi, float3 wo) {
    float sinTheta2Wi = sin2Theta(wi);
    float sinTheta2Wo = sin2Theta(wo);
    if (sinTheta2Wi <= 0.0 || sinTheta2Wo <= 0.0) return 0.0;

    float invSinProduct = rsqrt(sinTheta2Wi * sinTheta2Wo);
    return clamp((wi.x * wo.x + wi.y * wo.y) * invSinProduct, -1.0, 1.0);
}

#endif
