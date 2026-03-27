#include "../scene/resources.slang"
#include "payload.slang"

[shader("closesthit")] void main(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    Material material = materials[meshInfos[InstanceIndex()].materialIndex];
    payload.visibility =
        material.transmission > 0.0 ? VKRT_SHADOW_VISIBILITY_UNSUPPORTED_TRANSMISSION : VKRT_SHADOW_VISIBILITY_OCCLUDED;
}
