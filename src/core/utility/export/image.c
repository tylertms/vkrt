#include "image.h"

#include "constants.h"
#include "debug.h"
#include "denoise.h"
#include "exr.h"
#include "internal.h"
#include "io.h"
#include "vkrt_types.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <spng.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <turbojpeg.h>

static const int kJPEGQuality = 95;

typedef struct LinearRenderOutputRequest {
    const char* label;
    const RenderImageBuffer* beautyBuffer;
    const RenderImageBuffer* albedoBuffer;
    const RenderImageBuffer* normalBuffer;
    uint32_t width;
    uint32_t height;
    const VKRT_RenderExportSettings* settings;
    const VKRT_SceneSettingsSnapshot* sceneSettings;
    int allowRawFallback;
} LinearRenderOutputRequest;

static int tryComputePixelCount(uint32_t width, uint32_t height, size_t* outPixelCount) {
    if (!outPixelCount || width == 0u || height == 0u) return 0;
    size_t pixelCount = (size_t)width * (size_t)height;
    if (pixelCount / (size_t)width != (size_t)height) return 0;
    *outPixelCount = pixelCount;
    return 1;
}

static int tryComputeRGBAByteCount(uint32_t width, uint32_t height, size_t bytesPerChannel, size_t* outByteCount) {
    size_t pixelCount = 0u;
    if (!outByteCount || bytesPerChannel == 0u || !tryComputePixelCount(width, height, &pixelCount)) return 0;
    if (pixelCount > SIZE_MAX / 4u / bytesPerChannel) return 0;
    *outByteCount = pixelCount * 4u * bytesPerChannel;
    return 1;
}

int queryRenderImageBufferByteCount(
    uint32_t width,
    uint32_t height,
    RenderImageBufferFormat format,
    size_t* outByteCount
) {
    if (!outByteCount) return 0;
    switch (format) {
        case RENDER_IMAGE_BUFFER_FORMAT_RGBA32F:
            return tryComputeRGBAByteCount(width, height, sizeof(float), outByteCount);
        case RENDER_IMAGE_BUFFER_FORMAT_RGBA16F:
            return tryComputeRGBAByteCount(width, height, sizeof(uint16_t), outByteCount);
        default:
            return 0;
    }
}

static uint8_t convertUnorm16ToUnorm8(uint16_t value) {
    return (uint8_t)((((uint32_t)value * 255u) + 32767u) / 65535u);
}

static const char* queryPathExtension(const char* path) {
    const char* basename = pathBasename(path);
    if (!basename || !basename[0]) return NULL;

    const char* extension = strrchr(basename, '.');
    if (!extension || extension == basename || extension[1] == '\0') return NULL;
    return extension;
}

static int lowerASCII(int value) {
    return tolower((unsigned char)value);
}

static int equalsIgnoreCaseASCII(const char* lhs, const char* rhs) {
    if (!lhs || !rhs) return 0;
    while (*lhs && *rhs) {
        if (lowerASCII(*lhs) != lowerASCII(*rhs)) return 0;
        lhs++;
        rhs++;
    }
    return *lhs == '\0' && *rhs == '\0';
}

static int queryRenderImageFormatForPath(const char* path, RenderImageFormat* outFormat) {
    if (!path || !outFormat) return 0;

    const char* extension = queryPathExtension(path);
    if (!extension) return 0;

    if (equalsIgnoreCaseASCII(extension, ".png")) {
        *outFormat = RENDER_IMAGE_FORMAT_PNG;
        return 1;
    }
    if (equalsIgnoreCaseASCII(extension, ".jpg") || equalsIgnoreCaseASCII(extension, ".jpeg")) {
        *outFormat = RENDER_IMAGE_FORMAT_JPEG;
        return 1;
    }
    if (equalsIgnoreCaseASCII(extension, ".exr")) {
        *outFormat = RENDER_IMAGE_FORMAT_EXR;
        return 1;
    }
    return 0;
}

static char* duplicatePathWithAppendedExtension(const char* path, const char* extension) {
    if (!path || !path[0] || !extension || !extension[0]) return NULL;
    size_t pathLength = strlen(path);
    size_t extensionLength = strlen(extension);
    if (pathLength > SIZE_MAX - extensionLength - 1u) return NULL;

    char* combined = (char*)malloc(pathLength + extensionLength + 1u);
    if (!combined) return NULL;

    (void)snprintf(combined, pathLength + extensionLength + 1u, "%s%s", path, extension);
    return combined;
}

int resolveRenderImagePath(const char* requestedPath, char** outResolvedPath, RenderImageFormat* outFormat) {
    if (!requestedPath || !requestedPath[0] || !outResolvedPath || !outFormat) return 0;
    *outResolvedPath = NULL;

    if (queryRenderImageFormatForPath(requestedPath, outFormat)) {
        *outResolvedPath = stringDuplicate(requestedPath);
        return *outResolvedPath != NULL;
    }

    if (!queryPathExtension(requestedPath)) {
        *outFormat = RENDER_IMAGE_FORMAT_PNG;
        *outResolvedPath = duplicatePathWithAppendedExtension(requestedPath, ".png");
        return *outResolvedPath != NULL;
    }

    LOG_ERROR("Unsupported render export format: %s", requestedPath);
    return 0;
}

static int openBinaryFileForWrite(const char* path, FILE** outFile) {
    if (!path || !path[0] || !outFile) return 0;

    FILE* file = NULL;
#ifdef _WIN32
    if (fopen_s(&file, path, "wb") != 0) file = NULL;
#else
    file = fopen(path, "wb");
#endif
    if (!file) {
        LOG_ERROR("Failed to open export file: %s", path);
        return 0;
    }

    *outFile = file;
    return 1;
}

static int writeFileBytes(FILE* file, const void* data, size_t size) {
    if (!file || (!data && size != 0u)) return 0;
    return fwrite(data, 1u, size, file) == size;
}

static int writePNGFile(const char* path, const uint16_t* rgba16, uint32_t width, uint32_t height) {
    size_t rgba16ByteCount = 0u;
    if (!vkrtTryComputeImageByteCount(width, height, 8u, &rgba16ByteCount)) {
        LOG_ERROR("PNG export size overflow for '%s'", path);
        return 0;
    }

    FILE* file = NULL;
    if (!openBinaryFileForWrite(path, &file)) {
        return 0;
    }

    spng_ctx* context = spng_ctx_new(SPNG_CTX_ENCODER);
    if (!context) {
        (void)fclose(file);
        LOG_ERROR("PNG encoder initialization failed for '%s'", path);
        return 0;
    }

    int error = spng_set_png_file(context, file);
    if (error == 0) {
        struct spng_ihdr header = {
            .width = width,
            .height = height,
            .bit_depth = 16u,
            .color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA,
            .compression_method = 0u,
            .filter_method = 0u,
            .interlace_method = SPNG_INTERLACE_NONE,
        };
        error = spng_set_ihdr(context, &header);
    }
    if (error == 0) {
        error = spng_encode_image(context, rgba16, rgba16ByteCount, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    }

    spng_ctx_free(context);
    (void)fclose(file);

    if (error != 0) {
        LOG_ERROR("PNG export failed for '%s' (%s)", path, spng_strerror(error));
        return 0;
    }

    return 1;
}

static int writeJPEGFile(const char* path, const uint8_t* rgba8, uint32_t width, uint32_t height) {
    if (width > (uint32_t)INT_MAX || height > (uint32_t)INT_MAX) {
        LOG_ERROR("JPEG export dimensions exceed codec limits: %ux%u", width, height);
        return 0;
    }

    tjhandle handle = tjInitCompress();
    if (!handle) {
        LOG_ERROR("JPEG encoder initialization failed for '%s' (%s)", path, tjGetErrorStr());
        return 0;
    }

    unsigned char* encodedBytes = NULL;
    unsigned long encodedSize = 0ul;
    if (tjCompress2(
            handle,
            rgba8,
            (int)width,
            0,
            (int)height,
            TJPF_RGBA,
            &encodedBytes,
            &encodedSize,
            TJSAMP_444,
            kJPEGQuality,
            TJFLAG_ACCURATEDCT
        ) != 0) {
        LOG_ERROR("JPEG export failed for '%s' (%s)", path, tjGetErrorStr2(handle));
        tjDestroy(handle);
        return 0;
    }

    FILE* file = NULL;
    int writeOk = openBinaryFileForWrite(path, &file) && writeFileBytes(file, encodedBytes, (size_t)encodedSize);
    if (file) (void)fclose(file);

    tjFree(encodedBytes);
    tjDestroy(handle);

    if (!writeOk) {
        LOG_ERROR("Failed to write JPEG export file: %s", path);
        return 0;
    }

    return 1;
}

static int writeRenderImageFile(
    const char* path,
    const void* pixels,
    uint32_t width,
    uint32_t height,
    RenderImageFormat format
) {
    if (!path || !pixels || width == 0u || height == 0u) return -1;
    if (width > (uint32_t)INT32_MAX || height > (uint32_t)INT32_MAX) {
        LOG_ERROR("Image export dimensions exceed codec limits: %ux%u", width, height);
        return -1;
    }

    if (format == RENDER_IMAGE_FORMAT_EXR) {
        if (!vkrtWriteEXRFromRGBA32F(path, (const float*)pixels, width, height)) {
            (void)remove(path);
            LOG_ERROR("Render export failed for '%s'", path);
            return -1;
        }
        return 0;
    }

    const uint16_t* rgba16 = (const uint16_t*)pixels;
    if (format == RENDER_IMAGE_FORMAT_PNG) {
        if (!writePNGFile(path, rgba16, width, height)) {
            (void)remove(path);
            LOG_ERROR("Render export failed for '%s'", path);
            return -1;
        }
        return 0;
    }
    if (format != RENDER_IMAGE_FORMAT_JPEG) {
        LOG_ERROR("Unsupported render export format for '%s'", path);
        return -1;
    }

    size_t rgba8ByteCount = 0u;
    if (!vkrtTryComputeImageByteCount(width, height, 4u, &rgba8ByteCount)) {
        LOG_ERROR("Image export size overflow for '%s'", path);
        return -1;
    }

    uint8_t* rgba8 = (uint8_t*)malloc(rgba8ByteCount);
    if (!rgba8) {
        LOG_ERROR("Failed to allocate export buffer for '%s'", path);
        return -1;
    }

    for (size_t index = 0; index < rgba8ByteCount; index++) {
        rgba8[index] = convertUnorm16ToUnorm8(rgba16[index]);
    }

    int writeOk = writeJPEGFile(path, rgba8, width, height);
    free(rgba8);

    if (!writeOk) {
        (void)remove(path);
        LOG_ERROR("Render export failed for '%s'", path);
        return -1;
    }

    return 0;
}

static void freeRenderImageBuffer(RenderImageBuffer* buffer) {
    if (!buffer) return;
    free(buffer->pixels);
    buffer->pixels = NULL;
}

void freeRenderImageExportJob(RenderImageExportJob* job) {
    if (!job) return;
    free(job->path);
    freeRenderImageBuffer(&job->beauty);
    freeRenderImageBuffer(&job->albedo);
    freeRenderImageBuffer(&job->normal);
    free(job);
}

static float decodeHalfFloat(uint16_t value) {
    uint32_t sign = ((uint32_t)value & 0x8000u) << 16u;
    uint32_t exponent = ((uint32_t)value >> 10u) & 0x1fu;
    uint32_t mantissa = (uint32_t)value & 0x03ffu;
    uint32_t bits = 0u;

    if (exponent == 0u) {
        if (mantissa == 0u) {
            bits = sign;
        } else {
            exponent = 1u;
            while ((mantissa & 0x0400u) == 0u) {
                mantissa <<= 1u;
                exponent++;
            }
            mantissa &= 0x03ffu;
            bits = sign | ((127u - 15u - exponent + 1u) << 23u) | (mantissa << 13u);
        }
    } else if (exponent == 0x1fu) {
        bits = sign | 0x7f800000u | (mantissa << 13u);
    } else {
        bits = sign | ((exponent + (127u - 15u)) << 23u) | (mantissa << 13u);
    }

    float result = 0.0f;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static float clampf(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static float srgbEncodeScalar(float value) {
    value = clampf(value, 0.0f, 1.0f);
    if (value <= 0.0031308f) return 12.92f * value;
    return (1.055f * powf(value, 1.0f / 2.4f)) - 0.055f;
}

static void toneMapACES(const float* input, float* output) {
    if (!input || !output) return;
    for (uint32_t channel = 0; channel < 3u; channel++) {
        float color = input[channel];
        output[channel] = (color * ((2.51f * color) + 0.03f)) / ((color * ((2.43f * color) + 0.59f)) + 0.14f);
    }
}

static void toneMapSceneColor(const VKRT_SceneSettingsSnapshot* sceneSettings, const float* input, float* output) {
    if (!input || !output) return;

    if (sceneSettings && sceneSettings->toneMappingMode == VKRT_TONE_MAPPING_MODE_ACES) {
        toneMapACES(input, output);
        return;
    }

    output[0] = input[0];
    output[1] = input[1];
    output[2] = input[2];
}

static void encodeDisplayColor(const float* linear, float* encoded) {
    if (!linear || !encoded) return;
    encoded[0] = srgbEncodeScalar(linear[0]);
    encoded[1] = srgbEncodeScalar(linear[1]);
    encoded[2] = srgbEncodeScalar(linear[2]);
}

static uint16_t floatToUnorm16(float value) {
    value = clampf(value, 0.0f, 1.0f);
    return (uint16_t)((value * 65535.0f) + 0.5f);
}

static int prepareRGBA32FBuffer(
    const RenderImageBuffer* source,
    uint32_t width,
    uint32_t height,
    int clampNonNegative,
    int normalizeVectors,
    int preserveSourceWeight,
    float** outPixels
) {
    if (outPixels) *outPixels = NULL;
    if (!source || !source->pixels || !outPixels) return 0;

    size_t rgba32fByteCount = 0u;
    size_t pixelCount = 0u;
    if (!tryComputeRGBAByteCount(width, height, sizeof(float), &rgba32fByteCount) ||
        !tryComputePixelCount(width, height, &pixelCount)) {
        return 0;
    }

    float* converted = (float*)malloc(rgba32fByteCount);
    if (!converted) return 0;

    if (source->format == RENDER_IMAGE_BUFFER_FORMAT_RGBA32F) {
        memcpy(converted, source->pixels, rgba32fByteCount);
    } else if (source->format == RENDER_IMAGE_BUFFER_FORMAT_RGBA16F) {
        const uint16_t* halfPixels = (const uint16_t*)source->pixels;
        for (size_t i = 0; i < pixelCount * 4u; i++) {
            converted[i] = decodeHalfFloat(halfPixels[i]);
        }
    } else {
        free(converted);
        return 0;
    }

    size_t componentCount = pixelCount * 4u;
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; pixelIndex++) {
        size_t baseIndex = pixelIndex * 4u;
        if (baseIndex + 3u >= componentCount) {
            free(converted);
            return 0;
        }

        float* pixel = &converted[baseIndex];
        float sourceWeight = pixel[3];
        if (preserveSourceWeight) {
            pixel[3] = fmaxf(sourceWeight, 0.0f);
            if (pixel[3] <= 0.0f) {
                pixel[0] = 0.0f;
                pixel[1] = 0.0f;
                pixel[2] = 0.0f;
                continue;
            }
        }
        if (clampNonNegative) {
            pixel[0] = fmaxf(pixel[0], 0.0f);
            pixel[1] = fmaxf(pixel[1], 0.0f);
            pixel[2] = fmaxf(pixel[2], 0.0f);
        }
        if (normalizeVectors) {
            float lengthSquared = (pixel[0] * pixel[0]) + (pixel[1] * pixel[1]) + (pixel[2] * pixel[2]);
            if (lengthSquared > 1e-20f) {
                float invLength = 1.0f / sqrtf(lengthSquared);
                pixel[0] *= invLength;
                pixel[1] *= invLength;
                pixel[2] *= invLength;
            } else {
                pixel[0] = 0.0f;
                pixel[1] = 0.0f;
                pixel[2] = 0.0f;
            }
        }
        if (!preserveSourceWeight) {
            pixel[3] = 1.0f;
        }
    }

    *outPixels = converted;
    return 1;
}

static int cloneRGBA32FBuffer(const float* source, uint32_t width, uint32_t height, float** outPixels) {
    if (outPixels) *outPixels = NULL;
    if (!source || !outPixels) return 0;

    size_t rgba32fByteCount = 0u;
    if (!tryComputeRGBAByteCount(width, height, sizeof(float), &rgba32fByteCount)) {
        return 0;
    }

    float* clone = (float*)malloc(rgba32fByteCount);
    if (!clone) return 0;

    memcpy(clone, source, rgba32fByteCount);
    *outPixels = clone;
    return 1;
}

static int hasPreparedFeatureCoverage(const float* pixels, uint32_t width, uint32_t height) {
    size_t pixelCount = 0u;
    if (!pixels || !tryComputePixelCount(width, height, &pixelCount)) return 0;

    for (size_t pixelIndex = 0u; pixelIndex < pixelCount; pixelIndex++) {
        if (pixels[(pixelIndex * 4u) + 3u] > 0.0f) {
            return 1;
        }
    }

    return 0;
}

static int convertLinearToDisplayRGBA16(
    const float* linearPixels,
    uint32_t width,
    uint32_t height,
    const VKRT_SceneSettingsSnapshot* sceneSettings,
    uint16_t** outPixels
) {
    if (outPixels) *outPixels = NULL;
    if (!linearPixels || !sceneSettings || !outPixels) return 0;

    size_t rgba16ByteCount = 0u;
    size_t pixelCount = 0u;
    if (!tryComputeRGBAByteCount(width, height, sizeof(uint16_t), &rgba16ByteCount) ||
        !tryComputePixelCount(width, height, &pixelCount)) {
        return 0;
    }

    uint16_t* displayPixels = (uint16_t*)malloc(rgba16ByteCount);
    if (!displayPixels) return 0;

    int debugOutput = sceneSettings->debugMode != VKRT_DEBUG_MODE_NONE;
    float exposure = sceneSettings->exposure;
    if (!isfinite(exposure) || exposure < 0.0f) exposure = 1.0f;

    for (size_t pixelIndex = 0; pixelIndex < pixelCount; pixelIndex++) {
        const float* src = linearPixels + (pixelIndex * 4u);
        float mapped[3] = {src[0], src[1], src[2]};
        float encoded[3] = {0.0f, 0.0f, 0.0f};

        if (!debugOutput) {
            mapped[0] = src[0] * exposure;
            mapped[1] = src[1] * exposure;
            mapped[2] = src[2] * exposure;
            toneMapSceneColor(sceneSettings, mapped, mapped);
        }

        encodeDisplayColor(mapped, encoded);
        displayPixels[(pixelIndex * 4u) + 0u] = floatToUnorm16(encoded[0]);
        displayPixels[(pixelIndex * 4u) + 1u] = floatToUnorm16(encoded[1]);
        displayPixels[(pixelIndex * 4u) + 2u] = floatToUnorm16(encoded[2]);
        displayPixels[(pixelIndex * 4u) + 3u] = 65535u;
    }

    *outPixels = displayPixels;
    return 1;
}

static int prepareDenoiseFeatureBuffers(
    const LinearRenderOutputRequest* request,
    float** outAlbedo,
    float** outNormal
) {
    if (!request || !outAlbedo || !outNormal) return 0;

    *outAlbedo = NULL;
    *outNormal = NULL;
    if (!request->albedoBuffer || !request->albedoBuffer->pixels || !request->normalBuffer ||
        !request->normalBuffer->pixels) {
        return 0;
    }

    if (!prepareRGBA32FBuffer(request->albedoBuffer, request->width, request->height, 1, 0, 1, outAlbedo)) {
        return 0;
    }
    if (!prepareRGBA32FBuffer(request->normalBuffer, request->width, request->height, 0, 1, 1, outNormal)) {
        free(*outAlbedo);
        *outAlbedo = NULL;
        return 0;
    }

    if (!hasPreparedFeatureCoverage(*outAlbedo, request->width, request->height) &&
        !hasPreparedFeatureCoverage(*outNormal, request->width, request->height)) {
        free(*outNormal);
        free(*outAlbedo);
        *outNormal = NULL;
        *outAlbedo = NULL;
        return 0;
    }

    return 1;
}

static int prefilterDenoiseFeatureBuffer(
    const char* outputLabel,
    const char* featureLabel,
    VKRT_OIDNAuxImage auxImage,
    const float* source,
    uint32_t width,
    uint32_t height,
    float** outFiltered
) {
    if (outFiltered) *outFiltered = NULL;
    if (!source || !outFiltered) return 0;

    if (!cloneRGBA32FBuffer(source, width, height, outFiltered)) {
        LOG_ERROR("Failed to allocate prefiltered %s buffer for '%s'", featureLabel, outputLabel);
        return 0;
    }

    const char* errorMessage = NULL;
    if (vkrtOIDNPrefilterAux(auxImage, source, width, height, *outFiltered, &errorMessage)) {
        return 1;
    }

    LOG_INFO("OIDN %s prefilter failed for '%s'; using raw %s buffer", featureLabel, outputLabel, featureLabel);
    if (errorMessage && errorMessage[0]) {
        LOG_INFO("OIDN error detail: %s", errorMessage);
    }
    free(*outFiltered);
    *outFiltered = NULL;
    return 0;
}

static int denoiseLinearRenderOutput(
    const LinearRenderOutputRequest* request,
    const char* outputLabel,
    float** inOutLinearOutput
) {
    float* albedo = NULL;
    float* normal = NULL;
    float* prefilteredAlbedo = NULL;
    float* prefilteredNormal = NULL;
    float* denoised = NULL;
    size_t rgba32fByteCount = 0u;
    int featureBuffersReady = prepareDenoiseFeatureBuffers(request, &albedo, &normal);
    int albedoPrefiltered = 0;
    int normalPrefiltered = 0;
    int succeeded = 0;

    if (!tryComputeRGBAByteCount(request->width, request->height, sizeof(float), &rgba32fByteCount)) {
        goto cleanup;
    }

    denoised = (float*)malloc(rgba32fByteCount);
    if (!denoised) {
        LOG_ERROR("Failed to allocate denoised output buffer for '%s'", outputLabel);
        goto cleanup;
    }

    if (featureBuffersReady) {
        if (prefilterDenoiseFeatureBuffer(
                outputLabel,
                "albedo",
                VKRT_OIDN_AUX_IMAGE_ALBEDO,
                albedo,
                request->width,
                request->height,
                &prefilteredAlbedo
            )) {
            free(albedo);
            albedo = prefilteredAlbedo;
            prefilteredAlbedo = NULL;
            albedoPrefiltered = 1;
        }
        if (prefilterDenoiseFeatureBuffer(
                outputLabel,
                "normal",
                VKRT_OIDN_AUX_IMAGE_NORMAL,
                normal,
                request->width,
                request->height,
                &prefilteredNormal
            )) {
            free(normal);
            normal = prefilteredNormal;
            prefilteredNormal = NULL;
            normalPrefiltered = 1;
        }
    }

    const char* errorMessage = NULL;
    VKRT_OIDNFilterInput input = {
        .color = *inOutLinearOutput,
        .albedo = featureBuffersReady ? albedo : NULL,
        .normal = featureBuffersReady ? normal : NULL,
        .width = request->width,
        .height = request->height,
        .cleanAux = (uint8_t)(albedoPrefiltered && normalPrefiltered),
    };

    if (vkrtOIDNDenoise(&input, denoised, &errorMessage)) {
        free(*inOutLinearOutput);
        *inOutLinearOutput = denoised;
        denoised = NULL;
        succeeded = 1;
        goto cleanup;
    }

    if (!request->allowRawFallback) {
        LOG_ERROR("OIDN denoising failed for '%s'", outputLabel);
    } else {
        LOG_INFO("OIDN denoising failed for '%s'; using raw render", outputLabel);
    }
    if (errorMessage && errorMessage[0]) {
        LOG_INFO("OIDN error detail: %s", errorMessage);
    }
    if (request->allowRawFallback) {
        succeeded = 1;
    }

cleanup:
    free(prefilteredNormal);
    free(prefilteredAlbedo);
    free(denoised);
    free(normal);
    free(albedo);
    return succeeded;
}

static int prepareLinearRenderOutput(const LinearRenderOutputRequest* request, float** outLinearOutput) {
    if (outLinearOutput) *outLinearOutput = NULL;
    if (!request || !request->beautyBuffer || !request->beautyBuffer->pixels || request->width == 0u ||
        request->height == 0u || !request->settings || !request->sceneSettings || !outLinearOutput) {
        return 0;
    }

    const char* outputLabel = (request->label && request->label[0]) ? request->label : "render output";
    float* beauty = NULL;
    float* linearOutput = NULL;
    int result = 0;

    if (!prepareRGBA32FBuffer(request->beautyBuffer, request->width, request->height, 0, 0, 0, &beauty)) {
        LOG_ERROR("Failed to prepare beauty buffer for '%s'", outputLabel);
        goto cleanup;
    }
    linearOutput = beauty;
    beauty = NULL;

    int shouldDenoise =
        request->settings->denoiseEnabled != 0u && request->sceneSettings->debugMode == VKRT_DEBUG_MODE_NONE;

    if (shouldDenoise && !denoiseLinearRenderOutput(request, outputLabel, &linearOutput)) {
        goto cleanup;
    }

    *outLinearOutput = linearOutput;
    linearOutput = NULL;
    result = 1;

cleanup:
    free(linearOutput);
    free(beauty);
    return result;
}

int processRenderImageExportJob(RenderImageExportJob* job) {
    if (!job || !job->path || !job->beauty.pixels || job->width == 0u || job->height == 0u) return -1;

    float* linearOutput = NULL;
    uint16_t* displayPixels = NULL;
    int result = -1;
    LinearRenderOutputRequest request = {
        .label = job->path,
        .beautyBuffer = &job->beauty,
        .albedoBuffer = &job->albedo,
        .normalBuffer = &job->normal,
        .width = job->width,
        .height = job->height,
        .settings = &job->settings,
        .sceneSettings = &job->sceneSettings,
        .allowRawFallback = 1,
    };

    if (!prepareLinearRenderOutput(&request, &linearOutput)) {
        goto cleanup;
    }

    if (job->format == RENDER_IMAGE_FORMAT_EXR) {
        result = writeRenderImageFile(job->path, linearOutput, job->width, job->height, job->format);
        goto cleanup;
    }

    if (!convertLinearToDisplayRGBA16(linearOutput, job->width, job->height, &job->sceneSettings, &displayPixels)) {
        LOG_ERROR("Failed to tone-map render export for '%s'", job->path);
        goto cleanup;
    }

    result = writeRenderImageFile(job->path, displayPixels, job->width, job->height, job->format);

cleanup:
    free(displayPixels);
    free(linearOutput);
    return result;
}

int processViewportDenoiseJob(RenderImageExportJob* job, uint16_t** outPixels, size_t* outByteCount) {
    if (outPixels) *outPixels = NULL;
    if (outByteCount) *outByteCount = 0u;
    if (!job || !job->beauty.pixels || !outPixels || !outByteCount || job->width == 0u || job->height == 0u) {
        return -1;
    }

    float* linearOutput = NULL;
    uint16_t* displayPixels = NULL;
    size_t displayByteCount = 0u;
    int result = -1;
    LinearRenderOutputRequest request = {
        .label = "viewport denoise",
        .beautyBuffer = &job->beauty,
        .albedoBuffer = &job->albedo,
        .normalBuffer = &job->normal,
        .width = job->width,
        .height = job->height,
        .settings = &job->settings,
        .sceneSettings = &job->sceneSettings,
        .allowRawFallback = 0,
    };

    if (!prepareLinearRenderOutput(&request, &linearOutput)) {
        goto cleanup;
    }

    if (!convertLinearToDisplayRGBA16(linearOutput, job->width, job->height, &job->sceneSettings, &displayPixels)) {
        LOG_ERROR("Failed to tone-map viewport denoise result");
        goto cleanup;
    }

    if (!tryComputeRGBAByteCount(job->width, job->height, sizeof(uint16_t), &displayByteCount)) {
        LOG_ERROR("Viewport denoise output size overflow");
        goto cleanup;
    }

    *outPixels = displayPixels;
    *outByteCount = displayByteCount;
    displayPixels = NULL;
    result = 0;

cleanup:
    free(displayPixels);
    free(linearOutput);
    return result;
}

static void initializeRenderImageJob(RenderImageExportJob* job) {
    if (!job) return;

    job->beauty.format = RENDER_IMAGE_BUFFER_FORMAT_RGBA32F;
    job->albedo.format = RENDER_IMAGE_BUFFER_FORMAT_RGBA16F;
    job->normal.format = RENDER_IMAGE_BUFFER_FORMAT_RGBA16F;
}

RenderImageExportJob* createRenderImageJob(
    VKRT* vkrt,
    RenderImageJobType type,
    uint32_t width,
    uint32_t height,
    const VKRT_RenderExportSettings* settings
) {
    if (!vkrt || width == 0u || height == 0u || !settings) return NULL;

    RenderImageExportJob* job = (RenderImageExportJob*)calloc(1, sizeof(RenderImageExportJob));
    if (!job) return NULL;

    job->type = type;
    job->width = width;
    job->height = height;
    job->settings = *settings;
    job->sceneSettings = vkrt->sceneSettings;
    initializeRenderImageJob(job);
    return job;
}
