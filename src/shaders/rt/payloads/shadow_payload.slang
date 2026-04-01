#ifndef VKRT_RT_PAYLOAD_SHADOW_SLANG
#define VKRT_RT_PAYLOAD_SHADOW_SLANG

static const uint VKRT_SHADOW_VISIBILITY_OCCLUDED = 0u;
static const uint VKRT_SHADOW_VISIBILITY_VISIBLE = 1u;
static const uint VKRT_SHADOW_VISIBILITY_UNSUPPORTED_TRANSMISSION = 2u;

struct ShadowPayload {
    uint visibility;
    uint rng;

    __init(uint seed) {
        visibility = VKRT_SHADOW_VISIBILITY_OCCLUDED;
        rng = seed;
    }

    bool visible() {
        return visibility == VKRT_SHADOW_VISIBILITY_VISIBLE;
    }

    bool neeSupported() {
        return visibility != VKRT_SHADOW_VISIBILITY_UNSUPPORTED_TRANSMISSION;
    }
};

#endif
