#include "../../rt/payloads/shadow_payload.slang"

[shader("closesthit")] void main(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    payload.visibility =
        InstanceIndex() == payload.targetInstanceIndex && PrimitiveIndex() == payload.targetPrimitiveIndex
            ? VKRT_SHADOW_VISIBILITY_VISIBLE
            : VKRT_SHADOW_VISIBILITY_OCCLUDED;
}
