#include "denoise.h"

#include "debug.h"

#include <OpenImageDenoise/oidn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct VKRT_OIDNDenoiser {
    OIDNDevice device;
    OIDNBuffer buffers[3];
    OIDNFilter filters[3];
    int physicalDevice;
    uint32_t width;
    uint32_t height;
};

static _Thread_local char oidnErrorStorage[256];

VKRT_OIDNDenoiser* vkrtOIDNCreateDenoiser(void) {
    return (VKRT_OIDNDenoiser*)calloc(1u, sizeof(VKRT_OIDNDenoiser));
}

static void resetOIDNDenoiser(VKRT_OIDNDenoiser* denoiser) {
    for (size_t index = 0u; index < 3u; index++) {
        if (denoiser->filters[index]) oidnReleaseFilter(denoiser->filters[index]);
        if (denoiser->buffers[index]) oidnReleaseBuffer(denoiser->buffers[index]);
    }
    if (denoiser->device) oidnReleaseDevice(denoiser->device);
    memset(denoiser, 0, sizeof(*denoiser));
}

void vkrtOIDNDestroyDenoiser(VKRT_OIDNDenoiser* denoiser) {
    if (!denoiser) return;
    resetOIDNDenoiser(denoiser);
    free(denoiser);
}

static int reportOIDNError(const char** outErrorMessage, const char* message) {
    (void)snprintf(oidnErrorStorage, sizeof(oidnErrorStorage), "%s", message);
    if (outErrorMessage) *outErrorMessage = oidnErrorStorage;
    return 0;
}

static int checkOIDNError(OIDNDevice device, const char** outErrorMessage) {
    const char* message = NULL;
    if (oidnGetDeviceError(device, &message) == OIDN_ERROR_NONE) return 1;
    return reportOIDNError(outErrorMessage, message ? message : "OIDN operation failed");
}

static int findOIDNGPU(const uint8_t* deviceUUID) {
    int fallback = -1;
    int count = oidnGetNumPhysicalDevices();
    for (int index = 0; index < count; index++) {
        if (oidnGetPhysicalDeviceInt(index, "type") == OIDN_DEVICE_TYPE_CPU) continue;
        if (fallback < 0) fallback = index;
        if (deviceUUID && oidnGetPhysicalDeviceBool(index, "uuidSupported")) {
            size_t size = 0u;
            const void* uuid = oidnGetPhysicalDeviceData(index, "uuid", &size);
            if (uuid && size == OIDN_UUID_SIZE && memcmp(uuid, deviceUUID, size) == 0) return index;
        }
    }
    return fallback;
}

static void setOIDNImage(OIDNFilter filter, const char* name, OIDNBuffer buffer, const VKRT_OIDNFilterInput* input) {
    const size_t pixelStride = sizeof(float) * 4u;
    oidnSetFilterImage(
        filter,
        name,
        buffer,
        OIDN_FORMAT_FLOAT3,
        input->width,
        input->height,
        0u,
        pixelStride,
        pixelStride * (size_t)input->width
    );
}

static int prepareOIDNDenoiser(
    VKRT_OIDNDenoiser* denoiser,
    const VKRT_OIDNFilterInput* input,
    int physicalDevice,
    size_t byteCount,
    const char** outErrorMessage
) {
    if (denoiser->device && denoiser->physicalDevice == physicalDevice && denoiser->width == input->width &&
        denoiser->height == input->height) {
        return 1;
    }
    resetOIDNDenoiser(denoiser);
    denoiser->physicalDevice = physicalDevice;
    denoiser->width = input->width;
    denoiser->height = input->height;
    denoiser->device = physicalDevice >= 0 ? oidnNewDeviceByID(physicalDevice) : oidnNewDevice(OIDN_DEVICE_TYPE_CPU);
    OIDNDevice device = denoiser->device;
    if (!device) {
        if (!checkOIDNError(NULL, outErrorMessage)) return 0;
        return reportOIDNError(outErrorMessage, "Failed to create OIDN device");
    }

    OIDNBuffer* buffers = denoiser->buffers;
    OIDNFilter* filters = denoiser->filters;
    const char* names[3] = {"color", "albedo", "normal"};

    oidnCommitDevice(device);
    if (!checkOIDNError(device, outErrorMessage)) return 0;

    for (size_t index = 0u; index < 3u; index++) {
        buffers[index] = oidnNewBufferWithStorage(device, byteCount, OIDN_STORAGE_DEVICE);
        if (!checkOIDNError(device, outErrorMessage)) return 0;
        if (!buffers[index]) {
            return reportOIDNError(outErrorMessage, "Failed to allocate OIDN buffer");
        }
        filters[index] = oidnNewFilter(device, "RT");
        if (!checkOIDNError(device, outErrorMessage)) return 0;
        if (!filters[index]) {
            return reportOIDNError(outErrorMessage, "Failed to create OIDN filter");
        }
        setOIDNImage(filters[index], names[index], buffers[index], input);
        setOIDNImage(filters[index], "output", buffers[index], input);
    }

    setOIDNImage(filters[0], "albedo", buffers[1], input);
    setOIDNImage(filters[0], "normal", buffers[2], input);
    oidnSetFilterBool(filters[0], "hdr", true);
    oidnSetFilterBool(filters[0], "srgb", false);
    oidnSetFilterBool(filters[0], "cleanAux", true);
    for (size_t index = 0u; index < 3u; index++) {
        oidnSetFilterInt(filters[index], "quality", OIDN_QUALITY_HIGH);
        oidnCommitFilter(filters[index]);
        if (!checkOIDNError(device, outErrorMessage)) return 0;
    }
    return 1;
}

static int denoiseOnOIDNDevice(
    VKRT_OIDNDenoiser* denoiser,
    const VKRT_OIDNFilterInput* input,
    int physicalDevice,
    size_t byteCount,
    float* output,
    const char** outErrorMessage
) {
    if (!prepareOIDNDenoiser(denoiser, input, physicalDevice, byteCount, outErrorMessage)) goto failure;
    OIDNDevice device = denoiser->device;
    const float* sources[3] = {input->color, input->albedo, input->normal};
    for (size_t index = 0u; index < 3u; index++) {
        oidnWriteBuffer(denoiser->buffers[index], 0u, byteCount, sources[index]);
        if (!checkOIDNError(device, outErrorMessage)) goto failure;
    }
    const size_t order[3] = {1u, 2u, 0u};
    for (size_t index = 0u; index < 3u; index++) {
        oidnExecuteFilter(denoiser->filters[order[index]]);
        if (!checkOIDNError(device, outErrorMessage)) goto failure;
    }
    oidnReadBuffer(denoiser->buffers[0], 0u, byteCount, output);
    if (!checkOIDNError(device, outErrorMessage)) goto failure;

    LOG_INFO(
        "OIDN high-quality denoise completed on %s: %s",
        physicalDevice >= 0 ? "GPU" : "CPU",
        physicalDevice >= 0 ? oidnGetPhysicalDeviceString(physicalDevice, "name") : "CPU fallback"
    );
    return 1;

failure:
    resetOIDNDenoiser(denoiser);
    return 0;
}

int vkrtOIDNDenoise(
    VKRT_OIDNDenoiser* denoiser,
    const VKRT_OIDNFilterInput* input,
    float* output,
    const char** outErrorMessage
) {
    if (outErrorMessage) *outErrorMessage = NULL;
    if (!denoiser) return reportOIDNError(outErrorMessage, "Failed to allocate OIDN denoiser");
    if (!input || !output || !input->color || !input->albedo || !input->normal || input->width == 0u ||
        input->height == 0u) {
        return reportOIDNError(outErrorMessage, "OIDN requires color, albedo, and normal images");
    }
    if ((size_t)input->width > SIZE_MAX / (4u * sizeof(float)) / (size_t)input->height) {
        return reportOIDNError(outErrorMessage, "OIDN image size overflow");
    }

    const size_t byteCount = (size_t)input->width * (size_t)input->height * 4u * sizeof(float);
    int physicalDevice = findOIDNGPU(input->deviceUUID);
    if (!checkOIDNError(NULL, outErrorMessage)) {
        LOG_ERROR("OIDN GPU discovery failed: %s; using CPU", oidnErrorStorage);
        physicalDevice = -1;
    }
    if (physicalDevice >= 0) {
        if (denoiseOnOIDNDevice(denoiser, input, physicalDevice, byteCount, output, outErrorMessage)) return 1;
        LOG_ERROR("OIDN GPU denoise failed: %s; retrying on CPU", oidnErrorStorage);
    }
    if (outErrorMessage) *outErrorMessage = NULL;
    return denoiseOnOIDNDevice(denoiser, input, -1, byteCount, output, outErrorMessage);
}
