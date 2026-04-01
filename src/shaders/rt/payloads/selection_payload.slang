#ifndef VKRT_RT_PAYLOAD_SELECTION_SLANG
#define VKRT_RT_PAYLOAD_SELECTION_SLANG

struct SelectionPayload {
    uint hasHit;

    __init() {
        hasHit = 0u;
    }

    bool hit() {
        return hasHit != 0u;
    }
};

#endif
