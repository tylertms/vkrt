#include "buffer.h"
#include "debug.h"
#include "scene.h"
#include "types.h"
#include "vkrt_engine_types.h"
#include "vkrt_types.h"
#include "vulkan/vulkan_core.h"

#include <stdint.h>
#include <string.h>

extern const uint8_t rgb2specSrgbCoeffData[];
extern const size_t rgb2specSrgbCoeffSize;

static const uint32_t kRGB2SpecCoeffCount = 3u;

static VKRT_Result parseRGB2SpecHeader(
    const uint8_t* fileData,
    size_t fileSize,
    RGB2SpecTableInfo* outInfo,
    const float** outPayload,
    size_t* outPayloadSize
) {
    if (!fileData || !outInfo || !outPayload || !outPayloadSize) return VKRT_ERROR_INVALID_ARGUMENT;
    if (fileSize < 8u) {
        LOG_ERROR("Embedded RGB2Spec data is too small");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    if (memcmp(fileData, "SPEC", 4u) != 0) {
        LOG_ERROR("Embedded RGB2Spec data has invalid magic");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint32_t res = 0u;
    memcpy(&res, fileData + 4u, sizeof(res));
    if (res < 2u) {
        LOG_ERROR("Embedded RGB2Spec data has invalid resolution");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    uint64_t coeffValueCount =
        (uint64_t)3u * (uint64_t)res * (uint64_t)res * (uint64_t)res * (uint64_t)kRGB2SpecCoeffCount;
    uint64_t totalFloatCount = (uint64_t)res + coeffValueCount;
    uint64_t payloadSize64 = totalFloatCount * sizeof(float);
    if (payloadSize64 > SIZE_MAX || fileSize != 8u + (size_t)payloadSize64) {
        LOG_ERROR("Embedded RGB2Spec data has unexpected payload size");
        return VKRT_ERROR_OPERATION_FAILED;
    }

    *outInfo = (RGB2SpecTableInfo){
        .res = res,
        .scaleOffset = 0u,
        .dataOffset = res,
    };
    *outPayload = (const float*)(fileData + 8u);
    *outPayloadSize = (size_t)payloadSize64;
    return VKRT_SUCCESS;
}

VKRT_Result createRGB2SpecResources(VKRT* vkrt) {
    if (!vkrt) return VKRT_ERROR_INVALID_ARGUMENT;

    const float* payload = NULL;
    size_t payloadSize = 0u;
    RGB2SpecTableInfo info = {0};
    VKRT_Result result =
        parseRGB2SpecHeader(rgb2specSrgbCoeffData, rgb2specSrgbCoeffSize, &info, &payload, &payloadSize);
    if (result != VKRT_SUCCESS) return result;

    Buffer buffer = {0};
    result = createDeviceBufferFromDataImmediate(
        vkrt,
        payload,
        (VkDeviceSize)payloadSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        &buffer.buffer,
        &buffer.memory,
        &buffer.deviceAddress
    );
    if (result != VKRT_SUCCESS) return result;

    buffer.count = (uint32_t)(payloadSize / sizeof(float));
    vkrt->core.sceneRGB2SpecSRGBData = buffer;
    vkrt->core.rgb2specSRGBInfo = info;
    syncSceneStateData(vkrt);
    syncAllSceneDataFrames(vkrt);
    return VKRT_SUCCESS;
}
