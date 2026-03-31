#include "../../scene/resources.slang"

static const int VKRT_OUTLINE_GROUP_SIZE = 16;
static const int VKRT_MAX_OUTLINE_RADIUS = 5;
static const int VKRT_OUTLINE_SHARED_MASK_SPAN = VKRT_OUTLINE_GROUP_SIZE + VKRT_MAX_OUTLINE_RADIUS * 2;

groupshared uint sharedSelectionMask[VKRT_OUTLINE_SHARED_MASK_SPAN * VKRT_OUTLINE_SHARED_MASK_SPAN];

int outlineRadius(int2 size) {
    int minDimension = min(size.x, size.y);
    return clamp((int)round((float)minDimension * (2.0 / VKRT_OUTLINE_REFERENCE_HEIGHT)), 1, VKRT_MAX_OUTLINE_RADIUS);
}

uint sharedMaskIndex(int2 pixel) {
    return (uint)(pixel.y * VKRT_OUTLINE_SHARED_MASK_SPAN + pixel.x);
}

[shader("compute")][numthreads(16, 16, 1)] void main(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint3 groupThreadId : SV_GroupThreadID,
    uint3 groupId : SV_GroupID
) {
    uint width = 0;
    uint height = 0;
    selectionMaskImage.GetDimensions(width, height);

    int2 size = int2(width, height);
    int2 groupBase =
        int2(groupId.xy) * VKRT_OUTLINE_GROUP_SIZE - int2(VKRT_MAX_OUTLINE_RADIUS, VKRT_MAX_OUTLINE_RADIUS);
    for (int tileY = (int)groupThreadId.y; tileY < VKRT_OUTLINE_SHARED_MASK_SPAN; tileY += VKRT_OUTLINE_GROUP_SIZE) {
        for (int tileX = (int)groupThreadId.x; tileX < VKRT_OUTLINE_SHARED_MASK_SPAN;
             tileX += VKRT_OUTLINE_GROUP_SIZE) {
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
    int2 sharedPixel = int2(groupThreadId.xy) + int2(VKRT_MAX_OUTLINE_RADIUS, VKRT_MAX_OUTLINE_RADIUS);
    if (sharedSelectionMask[sharedMaskIndex(sharedPixel)] != 0u) return;

    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx == 0 && dy == 0) continue;
            if (sharedSelectionMask[sharedMaskIndex(sharedPixel + int2(dx, dy))] == 0u) continue;

            float4 base = outputImage[pixel];
            outputImage[pixel] = float4(VKRT_OUTLINE_COLOR, base.w);
            return;
        }
    }
}
