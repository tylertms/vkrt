#include "ggx_energy.h"

#include "buffer.h"
#include "scene.h"

#include <stdint.h>
#include <stdlib.h>
#include <zlib.h>

extern const uint8_t ggxEnergyData[];
extern const size_t ggxEnergySize;

VKRT_Result createGGXEnergyResources(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;
    uLongf payloadSize = VKRT_GGX_ENERGY_COUNT * sizeof(uint16_t);
    void* payload = malloc(payloadSize);
    if (!payload) return VKRT_ERROR_OUT_OF_MEMORY;
    int status = uncompress(payload, &payloadSize, ggxEnergyData, (uLong)ggxEnergySize);
    if (status != Z_OK || payloadSize != VKRT_GGX_ENERGY_COUNT * sizeof(uint16_t)) {
        free(payload);
        return VKRT_ERROR_OPERATION_FAILED;
    }

    Buffer* buffer = &vkrt->core.sceneGGXEnergyData;
    VKRT_Result result = createDeviceBufferFromDataImmediate(
        vkrt,
        payload,
        payloadSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        &buffer->buffer,
        &buffer->memory,
        NULL
    );
    free(payload);
    return result;
}
