#ifndef VKRT_UTILITY_VIEWPORT_SLANG
#define VKRT_UTILITY_VIEWPORT_SLANG

#include "../core/resources.slang"

bool insideViewport(int2 pixel) {
    int2 viewportOrigin = int2(scene.viewportRect.xy);
    int2 viewportSize = int2(scene.viewportRect.zw);
    if (viewportSize.x <= 0 || viewportSize.y <= 0) {
        return false;
    }

    int2 viewportMax = viewportOrigin + viewportSize;
    return pixel.x >= viewportOrigin.x &&
        pixel.y >= viewportOrigin.y &&
        pixel.x < viewportMax.x &&
        pixel.y < viewportMax.y;
}

#endif
