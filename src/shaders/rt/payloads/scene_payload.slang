#ifndef VKRT_RT_PAYLOAD_SCENE_SLANG
#define VKRT_RT_PAYLOAD_SCENE_SLANG

#include "../../scene/constants.slang"

struct SceneRayPayload {
    uint rng;
    uint instanceIndex;
    uint primitiveIndex;
    float hitDistance;
    float2 barycentrics;

    __init(uint seed) {
        rng = seed;
        instanceIndex = VKRT_INVALID_INDEX;
        primitiveIndex = VKRT_INVALID_INDEX;
        hitDistance = 0.0;
        barycentrics = float2(0.0);
    }

    bool hit() {
        return instanceIndex != VKRT_INVALID_INDEX;
    }

    [mutating] void setMiss() {
        instanceIndex = VKRT_INVALID_INDEX;
        primitiveIndex = VKRT_INVALID_INDEX;
        hitDistance = 0.0;
        barycentrics = float2(0.0);
    }

        [mutating] void setSurfaceHit(
            uint hitInstanceIndex,
            uint hitPrimitiveIndex,
            float distance,
            float2 hitBarycentrics
        ) {
        instanceIndex = hitInstanceIndex;
        primitiveIndex = hitPrimitiveIndex;
        hitDistance = distance;
        barycentrics = hitBarycentrics;
    }
};

#endif
