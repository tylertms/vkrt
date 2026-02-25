#pragma once

#include "vkrt.h"
#include "session.h"

#include <stdint.h>

enum { RENDER_SEQUENCE_PATH_CAPACITY = 1024 };

typedef struct RenderSequencer {
    uint8_t active;
    SessionRenderSettings renderSettings;
    uint32_t frameIndex;
    uint32_t frameCount;
    float minTime;
    float maxTime;
    float step;
    uint64_t frameStartTimeUs;
    uint32_t timedFrameCount;
    float averageFrameSeconds;
    char outputFolder[RENDER_SEQUENCE_PATH_CAPACITY];
} RenderSequencer;

void renderSequencerHandleCommands(RenderSequencer* sequencer, VKRT* vkrt, Session* session);
void renderSequencerUpdate(RenderSequencer* sequencer, VKRT* vkrt, Session* session);
