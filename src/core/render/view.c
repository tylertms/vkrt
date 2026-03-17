#include "view.h"

#include "numeric.h"

static VkExtent2D normalizeExtent(VkExtent2D extent) {
    VkExtent2D normalized = extent;
    if (normalized.width == 0) normalized.width = 1;
    if (normalized.height == 0) normalized.height = 1;
    return normalized;
}

void vkrtClampViewportRect(VkExtent2D extent, uint32_t* x, uint32_t* y, uint32_t* width, uint32_t* height) {
    if (!x || !y || !width || !height) return;

    if (extent.width == 0 || extent.height == 0) {
        *x = 0;
        *y = 0;
        *width = 0;
        *height = 0;
        return;
    }

    if (*width <= 1 || *height <= 1) {
        *x = 0;
        *y = 0;
        *width = extent.width;
        *height = extent.height;
    }

    if (*x >= extent.width) *x = extent.width - 1;
    if (*y >= extent.height) *y = extent.height - 1;
    if (*x + *width > extent.width) *width = extent.width - *x;
    if (*y + *height > extent.height) *height = extent.height - *y;

}

void vkrtQueryRenderViewCropExtent(
    VkExtent2D renderExtent,
    VkExtent2D viewportExtent,
    float zoom,
    uint32_t* outWidth,
    uint32_t* outHeight,
    VkBool32* outFillViewport
) {
    if (!outWidth || !outHeight) return;

    renderExtent = normalizeExtent(renderExtent);
    float renderWidth = (float)renderExtent.width;
    float renderHeight = (float)renderExtent.height;
    float clampedZoom = vkrtClampf(zoom, VKRT_RENDER_VIEW_ZOOM_MIN, VKRT_RENDER_VIEW_ZOOM_MAX);

    VkBool32 fillViewport = (clampedZoom > (VKRT_RENDER_VIEW_ZOOM_MIN + 0.0001f)) &&
                            viewportExtent.width > 0 &&
                            viewportExtent.height > 0;

    uint32_t cropWidth = renderExtent.width;
    uint32_t cropHeight = renderExtent.height;
    if (fillViewport) {
        float renderAspect = renderWidth / renderHeight;
        float viewAspect = (float)viewportExtent.width / (float)viewportExtent.height;
        float baseWidth = renderWidth;
        float baseHeight = renderHeight;
        if (viewAspect > renderAspect) {
            baseHeight = renderWidth / viewAspect;
        } else {
            baseWidth = renderHeight * viewAspect;
        }

        float cropWidthFloat = baseWidth / clampedZoom;
        float cropHeightFloat = baseHeight / clampedZoom;
        if (cropWidthFloat < 1.0f) cropWidthFloat = 1.0f;
        if (cropHeightFloat < 1.0f) cropHeightFloat = 1.0f;
        if (cropWidthFloat > renderWidth) cropWidthFloat = renderWidth;
        if (cropHeightFloat > renderHeight) cropHeightFloat = renderHeight;

        cropWidth = (uint32_t)(cropWidthFloat + 0.5f);
        cropHeight = (uint32_t)(cropHeightFloat + 0.5f);
    }

    if (cropWidth > renderExtent.width) cropWidth = renderExtent.width;
    if (cropHeight > renderExtent.height) cropHeight = renderExtent.height;

    *outWidth = cropWidth;
    *outHeight = cropHeight;
    if (outFillViewport) *outFillViewport = fillViewport;
}

void vkrtClampRenderViewPanOffset(
    VkExtent2D renderExtent,
    VkExtent2D viewportExtent,
    float zoom,
    float* panX,
    float* panY
) {
    if (!panX || !panY) return;

    renderExtent = normalizeExtent(renderExtent);
    uint32_t cropWidth = renderExtent.width;
    uint32_t cropHeight = renderExtent.height;
    vkrtQueryRenderViewCropExtent(renderExtent, viewportExtent, zoom, &cropWidth, &cropHeight, NULL);

    float maxPanX = ((float)renderExtent.width - (float)cropWidth) * 0.5f;
    float maxPanY = ((float)renderExtent.height - (float)cropHeight) * 0.5f;

    if (maxPanX <= 0.0f) {
        *panX = 0.0f;
    } else {
        *panX = vkrtClampf(*panX, -maxPanX, maxPanX);
    }

    if (maxPanY <= 0.0f) {
        *panY = 0.0f;
    } else {
        *panY = vkrtClampf(*panY, -maxPanY, maxPanY);
    }
}
