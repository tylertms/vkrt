#ifndef VKRT_UTILITY_SAMPLING_SLANG
#define VKRT_UTILITY_SAMPLING_SLANG

// References:
// MIS power heuristic: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency
// Alias method: Schwarz, 2011 - https://www.keithschwarz.com/darts-dice-coins/

float powerHeuristic(float pdfA, float pdfB) {
    float a2 = pdfA * pdfA;
    return a2 / (a2 + pdfB * pdfB);
}

uint sampleAlias(float u, uint count, uint offset, StructuredBuffer<float> aQ, StructuredBuffer<uint> aIdx) {
    float scaled = u * float(count);
    uint i = min(uint(scaled), count - 1u);
    float remainder = scaled - float(i);
    return (remainder < aQ[offset + i]) ? i : aIdx[offset + i];
}

#endif
