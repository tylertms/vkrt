#include "../../rt/payloads/scene_payload.slang"

[shader("miss")] void main(inout SceneRayPayload payload) { payload.setMiss(); }
