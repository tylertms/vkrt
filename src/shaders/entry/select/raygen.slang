#include "../../camera/ray.slang"
#include "../../camera/viewport.slang"
#include "../../rt/queries/selection_query.slang"

[shader("raygeneration")] void main() {
    int2 pixel = int2(DispatchRaysIndex().xy);
    if (scene.selectionEnabled == 0u || !insideViewport(pixel)) {
        selectionMaskImage[pixel] = 0u;
        return;
    }

    SelectionPayload payload = traceSelectionRay(makePrimaryRay(pixel, float2(0.0)));
    selectionMaskImage[pixel] = payload.hit() ? 1u : 0u;
}
