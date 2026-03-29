#pragma once

enum {
    VKRT_DEFAULT_WIDTH = 1600u,
    VKRT_DEFAULT_HEIGHT = 900u,
    VKRT_MAX_FRAMES_IN_FLIGHT = 2u,
    VKRT_FRAMETIME_HISTORY_SIZE = 128u,
};

static const float VKRT_RENDER_VIEW_ZOOM_MIN = 1.0f;
static const float VKRT_RENDER_VIEW_ZOOM_MAX = 64.0f;
