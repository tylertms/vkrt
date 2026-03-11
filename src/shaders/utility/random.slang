#ifndef VKRT_UTILITY_RANDOM_SLANG
#define VKRT_UTILITY_RANDOM_SLANG

uint hash(uint value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float rand(inout uint rng) {
    rng = hash(rng + 0x9e3779b9u);
    return float(rng & 0x00ffffffu) * (1.0 / 16777216.0);
}

uint initPixelSeed(int2 pixel, uint frameNumber, uint sampleIndex) {
    uint seed = uint(pixel.x) * 73856093u;
    seed ^= uint(pixel.y) * 19349663u;
    seed ^= frameNumber * 83492791u;
    seed ^= sampleIndex * 2654435761u;
    return hash(seed);
}

#endif
