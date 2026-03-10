#include "../../core/resources.slang"

[shader("anyhit")]
void main(inout Payload payload, in BuiltInTriangleIntersectionAttributes attr) {
    if (scene.selectionEnabled == 0u || InstanceIndex() != scene.selectedMeshIndex) {
        IgnoreHit();
    }
}
