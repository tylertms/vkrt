#include "../../trace/shadow.slang"

[shader("miss")]
void main(inout ShadowPayload payload) {
    payload.visible = 1u;
}
