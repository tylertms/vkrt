#ifndef VKRT_RT_SELECTION_PAYLOAD_SLANG
#define VKRT_RT_SELECTION_PAYLOAD_SLANG

struct SelectionPayload {
    uint didHit;

    __init() {
        didHit = 0u;
    }

    [mutating] void setMiss() { didHit = 0u; }

        [mutating] void setHit() {
        didHit = 1u;
    }
};

#endif
