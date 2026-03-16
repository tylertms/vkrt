#include "../../core/resources.slang"
#include "../../trace/selection.slang"
#include "../../utility/ray.slang"
#include "../../utility/viewport.slang"

[shader("raygeneration")]
void main() {
    int2 pixel = int2(DispatchRaysIndex().xy);
    if (scene.selectionEnabled == 0u || !insideViewport(pixel)) {
        selectionMaskImage[pixel] = 0u;
        return;
    }

    SelectionPayload payload = SelectionPayload.make();
    traceSelectionRay(payload, makePrimaryRay(pixel, float2(0.0)));
    selectionMaskImage[pixel] = payload.didHit != 0u ? 1u : 0u;
}
