#include "scene/resources.slang"
#include "payload.slang"

[shader("miss")]
void main(inout SelectionPayload payload) {
    payload.setMiss();
}
