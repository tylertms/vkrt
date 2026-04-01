#include "../../rt/payloads/shadow_payload.slang"

[shader("miss")] void main(inout ShadowPayload payload) { payload.visibility = VKRT_SHADOW_VISIBILITY_VISIBLE; }
