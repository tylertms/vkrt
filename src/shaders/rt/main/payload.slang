#ifndef VKRT_RT_MAIN_PAYLOAD_SLANG
#define VKRT_RT_MAIN_PAYLOAD_SLANG

#include "../../core/constants.slang"

struct Payload {
    uint didHit;
    uint didScatter;
    uint instanceIndex;
    float3 contribution;
    float3 bsdfWeight;
    float3 nextOrigin;
    float3 nextDirection;
    uint rngState;

    static Payload make(uint rngState) {
        Payload payload;
        payload.didHit = 0u;
        payload.didScatter = 0u;
        payload.instanceIndex = VKRT_INVALID_INDEX;
        payload.contribution = float3(0.0);
        payload.bsdfWeight = float3(0.0);
        payload.nextOrigin = float3(0.0);
        payload.nextDirection = float3(0.0);
        payload.rngState = rngState;
        return payload;
    }

    [mutating]
    void setMiss() {
        didHit = 0u;
        didScatter = 0u;
        instanceIndex = VKRT_INVALID_INDEX;
        contribution = float3(0.0);
        bsdfWeight = float3(0.0);
        nextOrigin = float3(0.0);
        nextDirection = float3(0.0);
    }

    [mutating]
    void setSurfaceHit(uint hitInstanceIndex, float3 surfaceContribution, float3 hitPoint) {
        didHit = 1u;
        didScatter = 0u;
        instanceIndex = hitInstanceIndex;
        contribution = surfaceContribution;
        bsdfWeight = float3(0.0);
        nextOrigin = hitPoint;
        nextDirection = float3(0.0);
    }

    [mutating]
    void setScatter(float3 weight, float3 origin, float3 direction) {
        didScatter = 1u;
        bsdfWeight = weight;
        nextOrigin = origin;
        nextDirection = direction;
    }
};

#endif
