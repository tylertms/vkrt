#include "../../rt/payloads/selection_payload.slang"
#include "../../scene/resources.slang"

[shader("miss")] void main(inout SelectionPayload payload) { payload.setMiss(); }
