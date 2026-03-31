#include "../../rt/payloads/scene_payload.slang"
#include "../../scene/resources.slang"

[shader("miss")] void main(inout SceneRayPayload payload) { payload.setMiss(); }
