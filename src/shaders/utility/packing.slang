#ifndef VKRT_UTILITY_PACKING_SLANG
#define VKRT_UTILITY_PACKING_SLANG

// References:
// Octahedron normal encoding: Cigolle et al., 2014 - https://jcgt.org/published/0003/02/01/

uint packOctNormal(float3 n) {
    n = normalize(n);
    float invL1 = 1.0 / (abs(n.x) + abs(n.y) + abs(n.z));
    float2 p = n.xy * invL1;

    if (n.z < 0.0) {
        float2 oldP = p;
        p.x = (1.0 - abs(oldP.y)) * (oldP.x >= 0.0 ? 1.0 : -1.0);
        p.y = (1.0 - abs(oldP.x)) * (oldP.y >= 0.0 ? 1.0 : -1.0);
    }

    int sx = int(round(clamp(p.x, -1.0, 1.0) * 32767.0));
    int sy = int(round(clamp(p.y, -1.0, 1.0) * 32767.0));
    return (uint(sx) & 0xffffu) | ((uint(sy) & 0xffffu) << 16);
}

float3 unpackOctNormal(uint packed) {
    float x = float(int(packed << 16) >> 16) / 32767.0;
    float y = float(int(packed) >> 16) / 32767.0;
    float z = 1.0 - abs(x) - abs(y);
    if (z < 0.0) {
        float oldX = x;
        x = (1.0 - abs(y)) * (oldX >= 0.0 ? 1.0 : -1.0);
        y = (1.0 - abs(oldX)) * (y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(float3(x, y, z));
}

#endif
