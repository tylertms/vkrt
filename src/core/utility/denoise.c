#include "denoise.h"

#include <OpenImageDenoise/oidn.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void setOIDNErrorMessage(const char** outErrorMessage, const char* message) {
    if (outErrorMessage) *outErrorMessage = message;
}

static _Thread_local char oidnErrorStorage[256];

typedef struct OIDNBufferSet {
    OIDNBuffer color;
    OIDNBuffer albedo;
    OIDNBuffer normal;
    OIDNBuffer output;
} OIDNBufferSet;

typedef struct OIDNFilterConfig {
    const char* mainImageName;
    uint8_t hdr;
    uint8_t srgb;
    uint8_t cleanAux;
} OIDNFilterConfig;

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
        (void)snprintf(oidnErrorStorage, sizeof(oidnErrorStorage), "failed to allocate OIDN %s buffer", label);
        setOIDNErrorMessage(outErrorMessage, oidnErrorStorage);
        return 0;
    }

    oidnWriteBuffer(buffer, 0u, byteCount, input);
    *outBuffer = buffer;
    return 1;
}

static int createOIDNOutputBuffer(
    OIDNDevice device,
    const float* seedInput,
    size_t byteCount,
    OIDNBuffer* outBuffer,
    const char** outErrorMessage
) {
    if (!device || !seedInput || !outBuffer) return 0;

    OIDNBuffer buffer = oidnNewBuffer(device, byteCount);
    if (!buffer) {
        setOIDNErrorMessage(outErrorMessage, "failed to allocate OIDN output buffer");
        return 0;
    }

    oidnWriteBuffer(buffer, 0u, byteCount, seedInput);
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
    const float* outputSeed,
    const char** outErrorMessage
) {
    if (!buffers) return 0;

    if (!createOIDNBuffer(device, input->color, imageByteCount, &buffers->color, outErrorMessage, "color")) {
        return 0;
    }

    if (!createOIDNOutputBuffer(device, outputSeed, imageByteCount, &buffers->output, outErrorMessage)) {
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
    const OIDNFilterConfig* config,
    const VKRT_OIDNFilterInput* input,
    OIDNBuffer colorBuffer,
    OIDNBuffer albedoBuffer,
    OIDNBuffer normalBuffer,
    OIDNBuffer outputBuffer
) {
    if (!config || !config->mainImageName || !config->mainImageName[0]) return;

    const size_t pixelStride = sizeof(float) * 4u;
    const size_t rowStride = pixelStride * (size_t)input->width;

    oidnSetFilterImage(
        filter,
        config->mainImageName,
        colorBuffer,
        OIDN_FORMAT_FLOAT3,
        input->width,
        input->height,
        0u,
        pixelStride,
        rowStride
    );
    if (input->albedo && strcmp(config->mainImageName, "albedo") != 0) {
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
    if (input->normal && strcmp(config->mainImageName, "normal") != 0) {
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
    oidnSetFilterBool(filter, "hdr", config->hdr != 0);
    oidnSetFilterBool(filter, "srgb", config->srgb != 0);
    oidnSetFilterBool(filter, "cleanAux", config->cleanAux != 0);
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
        (void)snprintf(oidnErrorStorage, sizeof(oidnErrorStorage), "%s", errorMessage);
        setOIDNErrorMessage(outErrorMessage, oidnErrorStorage);
    } else {
        setOIDNErrorMessage(outErrorMessage, "OIDN filtering failed");
    }
    return 0;
}

static int runOIDNFilter(
    const VKRT_OIDNFilterInput* input,
    const OIDNFilterConfig* config,
    float* output,
    const char** outErrorMessage
) {
    if (!input || !config || !output || !input->color || input->width == 0u || input->height == 0u) {
        setOIDNErrorMessage(outErrorMessage, "invalid OIDN input");
        return 0;
    }

    OIDNBufferSet buffers = {0};
    OIDNFilter filter = NULL;
    OIDNDevice device = NULL;
    int succeeded = 0;

    if (!createOIDNFilterContext(&device, &filter, outErrorMessage)) {
        goto cleanup;
    }

    const size_t imageByteCount = (sizeof(float) * 4u * (size_t)input->width) * (size_t)input->height;
    if (!createOIDNInputBuffers(device, input, imageByteCount, &buffers, input->color, outErrorMessage)) {
        goto cleanup;
    }

    configureOIDNFilterImages(filter, config, input, buffers.color, buffers.albedo, buffers.normal, buffers.output);
    if (!executeOIDNFilter(device, filter, buffers.output, imageByteCount, output, outErrorMessage)) {
        goto cleanup;
    }

    succeeded = 1;

cleanup:
    releaseOIDNResources(&device, &filter, &buffers.color, &buffers.albedo, &buffers.normal, &buffers.output);
    return succeeded;
}

int vkrtOIDNDenoise(const VKRT_OIDNFilterInput* input, float* output, const char** outErrorMessage) {
    setOIDNErrorMessage(outErrorMessage, NULL);
    if (!input || !output || !input->color || input->width == 0u || input->height == 0u) {
        setOIDNErrorMessage(outErrorMessage, "invalid OIDN input");
        return 0;
    }

    const OIDNFilterConfig config = {
        .mainImageName = "color",
        .hdr = 1u,
        .srgb = 0u,
        .cleanAux = input->cleanAux,
    };
    return runOIDNFilter(input, &config, output, outErrorMessage);
}

int vkrtOIDNPrefilterAux(
    VKRT_OIDNAuxImage auxImage,
    const float* input,
    uint32_t width,
    uint32_t height,
    float* output,
    const char** outErrorMessage
) {
    setOIDNErrorMessage(outErrorMessage, NULL);
    if (!input || !output || width == 0u || height == 0u) {
        setOIDNErrorMessage(outErrorMessage, "invalid OIDN auxiliary input");
        return 0;
    }

    const char* mainImageName = NULL;
    switch (auxImage) {
        case VKRT_OIDN_AUX_IMAGE_ALBEDO:
            mainImageName = "albedo";
            break;
        case VKRT_OIDN_AUX_IMAGE_NORMAL:
            mainImageName = "normal";
            break;
        default:
            setOIDNErrorMessage(outErrorMessage, "invalid OIDN auxiliary image kind");
            return 0;
    }

    const VKRT_OIDNFilterInput filterInput = {
        .color = input,
        .albedo = NULL,
        .normal = NULL,
        .width = width,
        .height = height,
        .cleanAux = 0u,
    };
    const OIDNFilterConfig config = {
        .mainImageName = mainImageName,
        .hdr = 0u,
        .srgb = 0u,
        .cleanAux = 0u,
    };
    return runOIDNFilter(&filterInput, &config, output, outErrorMessage);
}
