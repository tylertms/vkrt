#include "../../rt/payloads/selection_payload.slang"

[shader("miss")] void main(inout SelectionPayload payload) { payload.hasHit = 0u; }
