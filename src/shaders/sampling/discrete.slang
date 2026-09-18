#ifndef VKRT_UTILITY_SAMPLING_SLANG
#define VKRT_UTILITY_SAMPLING_SLANG

float powerHeuristic(float pdfA, float pdfB) {
    float scale = max(pdfA, pdfB);
    if (scale <= 0.0) return 0.0;
    pdfA /= scale;
    pdfB /= scale;
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
