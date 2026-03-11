#include "../../core/resources.slang"
#include "../../utility/ray.slang"
#include "../../utility/viewport.slang"
#include "payload.slang"

[shader("raygeneration")]
void main() {
    int2 pixel = int2(DispatchRaysIndex().xy);
    if (scene.selectionEnabled == 0u || !insideViewport(pixel)) {
        selectionMaskImage[pixel] = 0u;
        return;
    }

    SelectionPayload payload = SelectionPayload.make();

    TraceRay(topLevelAS, RAY_FLAG_FORCE_NON_OPAQUE, 0xffu, 0u, 0u, 0u, makePrimaryRay(pixel, float2(0.0)), payload);
    selectionMaskImage[pixel] = payload.didHit != 0u ? 1u : 0u;
}
