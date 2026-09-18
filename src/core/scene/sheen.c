#include "buffer.h"
#include "scene.h"
#include "sheen_ltc.h"

VKRT_Result createSheenResources(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    Buffer* buffer = &vkrt->core.sceneSheenLtcData;
    return createDeviceBufferFromDataImmediate(
        vkrt,
        SHEEN_LTC_TABLE,
        sizeof(SHEEN_LTC_TABLE),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        &buffer->buffer,
        &buffer->memory,
        NULL
    );
}
