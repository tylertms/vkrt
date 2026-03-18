#include "../scene/resources.slang"

static const int kGroupSize = 16;
static const int kMaxOutlineRadius = 5;
static const int kSharedMaskSpan = kGroupSize + kMaxOutlineRadius * 2;

groupshared uint sharedSelectionMask[kSharedMaskSpan * kSharedMaskSpan];

int outlineRadius(int2 size) {
    int minDimension = min(size.x, size.y);
    return clamp((int)round((float)minDimension * (2.0 / VKRT_OUTLINE_REFERENCE_HEIGHT)), 1, kMaxOutlineRadius);
}

uint sharedMaskIndex(int2 pixel) {
    return (uint)(pixel.y * kSharedMaskSpan + pixel.x);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint3 groupThreadId : SV_GroupThreadID,
    uint3 groupId : SV_GroupID
) {
    uint width = 0;
    uint height = 0;
    selectionMaskImage.GetDimensions(width, height);

    int2 size = int2(width, height);
    int2 groupBase = int2(groupId.xy) * kGroupSize - int2(kMaxOutlineRadius, kMaxOutlineRadius);
    for (int tileY = (int)groupThreadId.y; tileY < kSharedMaskSpan; tileY += kGroupSize) {
        for (int tileX = (int)groupThreadId.x; tileX < kSharedMaskSpan; tileX += kGroupSize) {
            int2 samplePixel = groupBase + int2(tileX, tileY);
            uint sampleValue = 0u;
            if (samplePixel.x >= 0 && samplePixel.y >= 0 && samplePixel.x < size.x && samplePixel.y < size.y) {
                sampleValue = selectionMaskImage[samplePixel];
            }
            sharedSelectionMask[sharedMaskIndex(int2(tileX, tileY))] = sampleValue;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    int2 pixel = int2(dispatchThreadId.xy);
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    int radius = outlineRadius(size);
    int2 sharedPixel = int2(groupThreadId.xy) + int2(kMaxOutlineRadius, kMaxOutlineRadius);
    if (sharedSelectionMask[sharedMaskIndex(sharedPixel)] != 0u) return;

    float3 outlineColor = VKRT_OUTLINE_COLOR;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx == 0 && dy == 0) continue;
            if (sharedSelectionMask[sharedMaskIndex(sharedPixel + int2(dx, dy))] == 0u) continue;

            float4 base = outputImage[pixel];
            outputImage[pixel] = float4(outlineColor, base.w);
            return;
        }
    }
}
