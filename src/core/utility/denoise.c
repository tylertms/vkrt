#include "denoise.h"

#if VKRT_OIDN_ENABLED
#include <OpenImageDenoise/oidn.h>
#endif

#include <stddef.h>
#include <stdio.h>

static void setOIDNErrorMessage(const char** outErrorMessage, const char* message) {
    if (outErrorMessage) *outErrorMessage = message;
}

#if VKRT_OIDN_ENABLED
static _Thread_local char oidnErrorStorage[256];

typedef struct OIDNBufferSet {
    OIDNBuffer color;
    OIDNBuffer albedo;
    OIDNBuffer normal;
    OIDNBuffer output;
} OIDNBufferSet;

static int createOIDNBuffer(
    OIDNDevice device,
    const float* input,
    size_t byteCount,
    OIDNBuffer* outBuffer,
    const char** outErrorMessage,
    const char* label
) {
    if (!device || !input || !outBuffer || !label || !label[0]) return 0;

    OIDNBuffer buffer = oidnNewBuffer(device, byteCount);
    if (!buffer) {
        snprintf(oidnErrorStorage, sizeof(oidnErrorStorage), "failed to allocate OIDN %s buffer", label);
        setOIDNErrorMessage(outErrorMessage, oidnErrorStorage);
        return 0;
    }

    oidnWriteBuffer(buffer, 0u, byteCount, input);
    *outBuffer = buffer;
    return 1;
}

static void releaseOIDNBuffer(OIDNBuffer* buffer) {
    if (!buffer || !*buffer) return;
    oidnReleaseBuffer(*buffer);
    *buffer = NULL;
}

static void releaseOIDNResources(
    OIDNDevice* device,
    OIDNFilter* filter,
    OIDNBuffer* colorBuffer,
    OIDNBuffer* albedoBuffer,
    OIDNBuffer* normalBuffer,
    OIDNBuffer* outputBuffer
) {
    releaseOIDNBuffer(outputBuffer);
    releaseOIDNBuffer(normalBuffer);
    releaseOIDNBuffer(albedoBuffer);
    releaseOIDNBuffer(colorBuffer);
    if (filter && *filter) {
        oidnReleaseFilter(*filter);
        *filter = NULL;
    }
    if (device && *device) {
        oidnReleaseDevice(*device);
        *device = NULL;
    }
}

static int createOIDNFilterContext(OIDNDevice* outDevice, OIDNFilter* outFilter, const char** outErrorMessage) {
    *outDevice = oidnNewDevice(OIDN_DEVICE_TYPE_CPU);
    if (!*outDevice) {
        setOIDNErrorMessage(outErrorMessage, "failed to create OIDN device");
        return 0;
    }
    oidnCommitDevice(*outDevice);

    *outFilter = oidnNewFilter(*outDevice, "RT");
    if (!*outFilter) {
        setOIDNErrorMessage(outErrorMessage, "failed to create OIDN RT filter");
        return 0;
    }

    return 1;
}

static int createOIDNInputBuffers(
    OIDNDevice device,
    const VKRT_OIDNFilterInput* input,
    size_t imageByteCount,
    OIDNBufferSet* buffers,
    const char** outErrorMessage
) {
    if (!buffers) return 0;

    if (!createOIDNBuffer(device, input->color, imageByteCount, &buffers->color, outErrorMessage, "color")) {
        return 0;
    }

    buffers->output = oidnNewBuffer(device, imageByteCount);
    if (!buffers->output) {
        setOIDNErrorMessage(outErrorMessage, "failed to allocate OIDN output buffer");
        return 0;
    }

    if (input->albedo &&
        !createOIDNBuffer(device, input->albedo, imageByteCount, &buffers->albedo, outErrorMessage, "albedo")) {
        return 0;
    }
    if (input->normal &&
        !createOIDNBuffer(device, input->normal, imageByteCount, &buffers->normal, outErrorMessage, "normal")) {
        return 0;
    }

    return 1;
}

static void configureOIDNFilterImages(
    OIDNFilter filter,
    const VKRT_OIDNFilterInput* input,
    OIDNBuffer colorBuffer,
    OIDNBuffer albedoBuffer,
    OIDNBuffer normalBuffer,
    OIDNBuffer outputBuffer
) {
    const size_t pixelStride = sizeof(float) * 4u;
    const size_t rowStride = pixelStride * (size_t)input->width;

    oidnSetFilterImage(
        filter,
        "color",
        colorBuffer,
        OIDN_FORMAT_FLOAT3,
        input->width,
        input->height,
        0u,
        pixelStride,
        rowStride
    );
    if (input->albedo) {
        oidnSetFilterImage(
            filter,
            "albedo",
            albedoBuffer,
            OIDN_FORMAT_FLOAT3,
            input->width,
            input->height,
            0u,
            pixelStride,
            rowStride
        );
    }
    if (input->normal) {
        oidnSetFilterImage(
            filter,
            "normal",
            normalBuffer,
            OIDN_FORMAT_FLOAT3,
            input->width,
            input->height,
            0u,
            pixelStride,
            rowStride
        );
    }
    oidnSetFilterImage(
        filter,
        "output",
        outputBuffer,
        OIDN_FORMAT_FLOAT3,
        input->width,
        input->height,
        0u,
        pixelStride,
        rowStride
    );
    oidnSetFilterBool(filter, "hdr", true);
    oidnSetFilterBool(filter, "srgb", false);
    oidnSetFilterBool(filter, "cleanAux", input->cleanAux != 0);
    oidnSetFilterInt(filter, "quality", OIDN_QUALITY_HIGH);
}

static int executeOIDNFilter(
    OIDNDevice device,
    OIDNFilter filter,
    OIDNBuffer outputBuffer,
    size_t imageByteCount,
    float* output,
    const char** outErrorMessage
) {
    oidnCommitFilter(filter);
    oidnExecuteFilter(filter);
    oidnSyncDevice(device);

    const char* errorMessage = NULL;
    OIDNError error = oidnGetDeviceError(device, &errorMessage);
    if (error == OIDN_ERROR_NONE) {
        oidnReadBuffer(outputBuffer, 0u, imageByteCount, output);
        error = oidnGetDeviceError(device, &errorMessage);
    }
    if (error == OIDN_ERROR_NONE) {
        return 1;
    }

    if (errorMessage && errorMessage[0]) {
        snprintf(oidnErrorStorage, sizeof(oidnErrorStorage), "%s", errorMessage);
        setOIDNErrorMessage(outErrorMessage, oidnErrorStorage);
    } else {
        setOIDNErrorMessage(outErrorMessage, "OIDN filtering failed");
    }
    return 0;
}
#endif

int vkrtOIDNAvailable(void) {
#if VKRT_OIDN_ENABLED
    return 1;
#else
    return 0;
#endif
}

int vkrtOIDNDenoise(const VKRT_OIDNFilterInput* input, float* output, const char** outErrorMessage) {
    setOIDNErrorMessage(outErrorMessage, NULL);
    if (!input || !output || !input->color || input->width == 0u || input->height == 0u) {
        setOIDNErrorMessage(outErrorMessage, "invalid OIDN input");
        return 0;
    }

#if !VKRT_OIDN_ENABLED
    setOIDNErrorMessage(outErrorMessage, "OIDN support was not compiled into this build");
    return 0;
#else
    OIDNBufferSet buffers = {0};
    OIDNFilter filter = NULL;
    OIDNDevice device = NULL;
    int succeeded = 0;

    if (!createOIDNFilterContext(&device, &filter, outErrorMessage)) {
        goto cleanup;
    }

    const size_t imageByteCount = (sizeof(float) * 4u * (size_t)input->width) * (size_t)input->height;
    if (!createOIDNInputBuffers(device, input, imageByteCount, &buffers, outErrorMessage)) {
        goto cleanup;
    }

    configureOIDNFilterImages(filter, input, buffers.color, buffers.albedo, buffers.normal, buffers.output);
    if (!executeOIDNFilter(device, filter, buffers.output, imageByteCount, output, outErrorMessage)) {
        goto cleanup;
    }

    succeeded = 1;

cleanup:
    releaseOIDNResources(&device, &filter, &buffers.color, &buffers.albedo, &buffers.normal, &buffers.output);
    return succeeded;
#endif
}
