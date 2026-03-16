#include "../../core/resources.slang"
#include "payload.slang"

[shader("miss")]
void main(inout Payload payload) {
    payload.setMiss();
}
