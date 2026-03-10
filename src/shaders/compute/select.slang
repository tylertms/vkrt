#include "../core/resources.slang"

int outlineRadius(int2 size) {
    int minDimension = min(size.x, size.y);
    return clamp((int)round((float)minDimension * (2.5 / 1080.0)), 1, 5);
}

bool isSelected(int2 pixel) {
    return selectionMaskImage[pixel] != 0u;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint width = 0;
    uint height = 0;
    selectionMaskImage.GetDimensions(width, height);

    int2 size = int2(width, height);
    int2 pixel = int2(dispatchThreadId.xy);
    if (pixel.x >= size.x || pixel.y >= size.y) return;
    if (isSelected(pixel)) return;

    int radius = outlineRadius(size);
    float3 outlineColor = float3(1.0, 0.55, 0.1);
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx == 0 && dy == 0) continue;

            int2 samplePixel = pixel + int2(dx, dy);
            if (samplePixel.x < 0 || samplePixel.y < 0 || samplePixel.x >= size.x || samplePixel.y >= size.y) continue;
            if (!isSelected(samplePixel)) continue;

            float4 base = outputImage[pixel];
            outputImage[pixel] = float4(outlineColor, base.w);
            return;
        }
    }
}
