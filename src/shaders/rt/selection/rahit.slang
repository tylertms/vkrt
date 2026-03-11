#include "../../core/resources.slang"
#include "payload.slang"

[shader("anyhit")]
void main(inout SelectionPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    if (scene.selectionEnabled == 0u || InstanceIndex() != scene.selectedMeshIndex) {
        IgnoreHit();
    }
}
