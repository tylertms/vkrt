#include "../../core/resources.slang"

[shader("miss")]
void main(inout Payload payload) {
    payload.radiance = float3(0.0);
    payload.normal = float3(0.0);
    payload.didHit = 0u;
    payload.instanceIndex = VKRT_INVALID_INDEX;
    payload.hitDistance = 0.0;
}
