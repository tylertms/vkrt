#include "image.h"

#include "debug.h"
#include "exr.h"

#include <spng.h>
#include <turbojpeg.h>

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum ImageCodec {
    IMAGE_CODEC_UNKNOWN = 0,
    IMAGE_CODEC_PNG,
    IMAGE_CODEC_JPEG,
    IMAGE_CODEC_EXR,
} ImageCodec;

static int mimeTypeStartsWith(const char* mimeType, const char* prefix) {
    if (!mimeType || !prefix) return 0;
    size_t prefixLength = strlen(prefix);
    return strncmp(mimeType, prefix, prefixLength) == 0;
}

static const char* imageSourceLabel(const char* mimeType) {
    return mimeType && mimeType[0] ? mimeType : "memory";
}

int vkrtTryComputeImageByteCount(uint32_t width, uint32_t height, uint32_t channels, size_t* outByteCount) {
    if (!outByteCount || width == 0u || height == 0u || channels == 0u) return 0;
    size_t pixelCount = (size_t)width * (size_t)height;
    if (pixelCount > SIZE_MAX / (size_t)channels) return 0;
    *outByteCount = pixelCount * (size_t)channels;
    return 1;
}

static int finalizeLoadedImage(
    const char* sourceLabel,
    void* pixels,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t colorSpace,
    VKRT_LoadedImage* outImage
) {
    if (!pixels || !outImage || width == 0u || height == 0u) {
        free(pixels);
        LOG_ERROR("Decoded image from %s had invalid dimensions", sourceLabel);
        return 0;
    }

    outImage->pixels = pixels;
    outImage->width = width;
    outImage->height = height;
    outImage->format = format;
    outImage->colorSpace = colorSpace;
    return 1;
}

static float srgbDecodeScalar(float value) {
    if (value <= 0.04045f) return value / 12.92f;
    return powf((value + 0.055f) / 1.055f, 2.4f);
}

static void convertSRGB16PNGToLinearRGBA16(uint16_t* rgba16, uint32_t width, uint32_t height) {
    if (!rgba16 || width == 0u || height == 0u) return;

    size_t pixelCount = (size_t)width * (size_t)height;
    for (size_t i = 0; i < pixelCount; i++) {
        for (size_t channel = 0; channel < 3u; channel++) {
            float encoded = (float)rgba16[i * 4u + channel] / 65535.0f;
            float linear = srgbDecodeScalar(encoded);
            long unorm16 = lrintf(linear * 65535.0f);
            if (unorm16 < 0l) unorm16 = 0l;
            if (unorm16 > 65535l) unorm16 = 65535l;
            rgba16[i * 4u + channel] = (uint16_t)unorm16;
        }
    }
}

static ImageCodec queryImageCodecFromMimeType(const char* mimeType) {
    if (!mimeType || !mimeType[0]) return IMAGE_CODEC_UNKNOWN;
    if (mimeTypeStartsWith(mimeType, "image/png")) return IMAGE_CODEC_PNG;
    if (mimeTypeStartsWith(mimeType, "image/jpeg")) return IMAGE_CODEC_JPEG;
    if (mimeTypeStartsWith(mimeType, "image/exr") || mimeTypeStartsWith(mimeType, "image/x-exr")) return IMAGE_CODEC_EXR;
    return IMAGE_CODEC_UNKNOWN;
}

static ImageCodec queryImageCodecFromBytes(const uint8_t* data, size_t size) {
    static const uint8_t pngSignature[8] = {137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u};
    static const uint8_t jpegSignature[3] = {0xffu, 0xd8u, 0xffu};
    static const uint8_t exrSignature[4] = {0x76u, 0x2fu, 0x31u, 0x01u};

    if (!data || size == 0u) return IMAGE_CODEC_UNKNOWN;
    if (size >= sizeof(pngSignature) && memcmp(data, pngSignature, sizeof(pngSignature)) == 0) {
        return IMAGE_CODEC_PNG;
    }
    if (size >= sizeof(jpegSignature) && memcmp(data, jpegSignature, sizeof(jpegSignature)) == 0) {
        return IMAGE_CODEC_JPEG;
    }
    if (size >= sizeof(exrSignature) && memcmp(data, exrSignature, sizeof(exrSignature)) == 0) {
        return IMAGE_CODEC_EXR;
    }
    return IMAGE_CODEC_UNKNOWN;
}

static int reportUnsupportedImageFormat(const char* sourceLabel, const char* mimeType) {
    if (mimeType && mimeType[0]) {
        LOG_ERROR("Unsupported image format for %s (%s)", sourceLabel, mimeType);
    } else {
        LOG_ERROR("Unsupported image format for %s", sourceLabel);
    }
    return 0;
}

static int decodePNGImage(
    const void* data,
    size_t size,
    const char* sourceLabel,
    uint32_t preferredColorSpace,
    VKRT_LoadedImage* outImage
) {
    spng_ctx* context = spng_ctx_new(0);
    if (!context) {
        LOG_ERROR("PNG decoder initialization failed for %s", sourceLabel);
        return 0;
    }

    int error = spng_set_png_buffer(context, data, size);
    if (error != 0) {
        LOG_ERROR("PNG decode from %s failed (%s)", sourceLabel, spng_strerror(error));
        spng_ctx_free(context);
        return 0;
    }

    struct spng_ihdr header = {0};
    error = spng_get_ihdr(context, &header);
    if (error != 0) {
        LOG_ERROR("PNG header decode from %s failed (%s)", sourceLabel, spng_strerror(error));
        spng_ctx_free(context);
        return 0;
    }

    int decodeAsRGBA16 = header.bit_depth == 16u;
    uint32_t format = decodeAsRGBA16
        ? VKRT_TEXTURE_FORMAT_RGBA16_UNORM
        : VKRT_TEXTURE_FORMAT_RGBA8_UNORM;
    int decodeFormat = decodeAsRGBA16 ? SPNG_FMT_RGBA16 : SPNG_FMT_RGBA8;
    size_t rgbaByteCount = 0u;
    error = spng_decoded_image_size(context, decodeFormat, &rgbaByteCount);
    if (error != 0) {
        LOG_ERROR("PNG output sizing failed for %s (%s)", sourceLabel, spng_strerror(error));
        spng_ctx_free(context);
        return 0;
    }

    size_t expectedByteCount = 0u;
    if (!vkrtTryComputeImageByteCount(
        header.width,
        header.height,
        decodeAsRGBA16 ? 8u : 4u,
        &expectedByteCount
    ) || rgbaByteCount != expectedByteCount) {
        LOG_ERROR("PNG image dimensions overflow for %s", sourceLabel);
        spng_ctx_free(context);
        return 0;
    }

    void* pixels = malloc(rgbaByteCount);
    if (!pixels) {
        LOG_ERROR("Failed to allocate PNG decode buffer for %s", sourceLabel);
        spng_ctx_free(context);
        return 0;
    }

    error = spng_decode_image(context, pixels, rgbaByteCount, decodeFormat, 0);
    spng_ctx_free(context);
    if (error != 0) {
        free(pixels);
        LOG_ERROR("PNG decode from %s failed (%s)", sourceLabel, spng_strerror(error));
        return 0;
    }

    uint32_t storageColorSpace = preferredColorSpace;
    if (decodeAsRGBA16 && preferredColorSpace == VKRT_TEXTURE_COLOR_SPACE_SRGB) {
        convertSRGB16PNGToLinearRGBA16((uint16_t*)pixels, header.width, header.height);
        storageColorSpace = VKRT_TEXTURE_COLOR_SPACE_LINEAR;
    }

    return finalizeLoadedImage(sourceLabel, pixels, header.width, header.height, format, storageColorSpace, outImage);
}

static int decodeJPEGImage(
    const void* data,
    size_t size,
    const char* sourceLabel,
    uint32_t preferredColorSpace,
    VKRT_LoadedImage* outImage
) {
    if (size > (size_t)ULONG_MAX) {
        LOG_ERROR("JPEG input too large for %s", sourceLabel);
        return 0;
    }

    tjhandle handle = tjInitDecompress();
    if (!handle) {
        LOG_ERROR("JPEG decoder initialization failed for %s (%s)", sourceLabel, tjGetErrorStr());
        return 0;
    }

    int width = 0;
    int height = 0;
    int subsampling = 0;
    int colorspace = 0;
    if (tjDecompressHeader3(
        handle,
        (const unsigned char*)data,
        (unsigned long)size,
        &width,
        &height,
        &subsampling,
        &colorspace
    ) != 0) {
        LOG_ERROR("JPEG header decode from %s failed (%s)", sourceLabel, tjGetErrorStr2(handle));
        tjDestroy(handle);
        return 0;
    }

    (void)subsampling;
    (void)colorspace;

    if (width <= 0 || height <= 0) {
        LOG_ERROR("Decoded JPEG from %s had invalid dimensions", sourceLabel);
        tjDestroy(handle);
        return 0;
    }

    size_t rgbaByteCount = 0u;
    if (!vkrtTryComputeImageByteCount((uint32_t)width, (uint32_t)height, 4u, &rgbaByteCount)) {
        LOG_ERROR("JPEG image dimensions overflow for %s", sourceLabel);
        tjDestroy(handle);
        return 0;
    }

    uint8_t* pixels = (uint8_t*)malloc(rgbaByteCount);
    if (!pixels) {
        LOG_ERROR("Failed to allocate JPEG decode buffer for %s", sourceLabel);
        tjDestroy(handle);
        return 0;
    }

    if (tjDecompress2(
        handle,
        (const unsigned char*)data,
        (unsigned long)size,
        pixels,
        width,
        0,
        height,
        TJPF_RGBA,
        TJFLAG_ACCURATEDCT
    ) != 0) {
        free(pixels);
        LOG_ERROR("JPEG decode from %s failed (%s)", sourceLabel, tjGetErrorStr2(handle));
        tjDestroy(handle);
        return 0;
    }

    tjDestroy(handle);
    return finalizeLoadedImage(
        sourceLabel,
        pixels,
        (uint32_t)width,
        (uint32_t)height,
        VKRT_TEXTURE_FORMAT_RGBA8_UNORM,
        preferredColorSpace,
        outImage
    );
}

static int decodeEXRImage(
    const void* data,
    size_t size,
    const char* sourceLabel,
    VKRT_LoadedImage* outImage
) {
    return vkrtLoadEXRImageFromMemory(data, size, sourceLabel, outImage);
}

static int decodeImageBytes(
    const void* data,
    size_t size,
    const char* mimeType,
    const char* sourceLabel,
    uint32_t preferredColorSpace,
    VKRT_LoadedImage* outImage
) {
    ImageCodec codec = queryImageCodecFromMimeType(mimeType);
    if (codec == IMAGE_CODEC_UNKNOWN) {
        codec = queryImageCodecFromBytes((const uint8_t*)data, size);
    }

    switch (codec) {
        case IMAGE_CODEC_PNG:
            return decodePNGImage(data, size, sourceLabel, preferredColorSpace, outImage);
        case IMAGE_CODEC_JPEG:
            return decodeJPEGImage(data, size, sourceLabel, preferredColorSpace, outImage);
        case IMAGE_CODEC_EXR:
            return decodeEXRImage(data, size, sourceLabel, outImage);
        case IMAGE_CODEC_UNKNOWN:
        default:
            return reportUnsupportedImageFormat(sourceLabel, mimeType);
    }
}

static int readBinaryFile(const char* path, uint8_t** outData, size_t* outSize) {
    if (!path || !path[0] || !outData || !outSize) return 0;
    *outData = NULL;
    *outSize = 0u;

    FILE* file = NULL;
#if defined(_WIN32)
    if (fopen_s(&file, path, "rb") != 0) file = NULL;
#else
    file = fopen(path, "rb");
#endif
    if (!file) {
        LOG_ERROR("Failed to open image file: %s", path);
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        LOG_ERROR("Failed to seek image file: %s", path);
        fclose(file);
        return 0;
    }

    long fileLength = ftell(file);
    if (fileLength <= 0) {
        LOG_ERROR("Failed to determine image file size: %s", path);
        fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        LOG_ERROR("Failed to rewind image file: %s", path);
        fclose(file);
        return 0;
    }

    size_t bufferSize = (size_t)fileLength;
    uint8_t* buffer = (uint8_t*)malloc(bufferSize);
    if (!buffer) {
        LOG_ERROR("Failed to allocate image file buffer for %s", path);
        fclose(file);
        return 0;
    }

    size_t bytesRead = fread(buffer, 1u, bufferSize, file);
    fclose(file);
    if (bytesRead != bufferSize) {
        free(buffer);
        LOG_ERROR("Failed to read image file: %s", path);
        return 0;
    }

    *outData = buffer;
    *outSize = bufferSize;
    return 1;
}

int vkrtLoadImageFromFile(const char* path, uint32_t preferredColorSpace, VKRT_LoadedImage* outImage) {
    if (!path || !path[0] || !outImage) return 0;
    *outImage = (VKRT_LoadedImage){0};

    uint8_t* encodedBytes = NULL;
    size_t encodedSize = 0u;
    if (!readBinaryFile(path, &encodedBytes, &encodedSize)) {
        return 0;
    }

    int result = decodeImageBytes(encodedBytes, encodedSize, NULL, path, preferredColorSpace, outImage);
    free(encodedBytes);
    return result;
}

int vkrtLoadImageFromMemory(
    const void* data,
    size_t size,
    const char* mimeType,
    uint32_t preferredColorSpace,
    VKRT_LoadedImage* outImage
) {
    if (!data || size == 0u || !outImage) return 0;
    *outImage = (VKRT_LoadedImage){0};
    return decodeImageBytes(data, size, mimeType, imageSourceLabel(mimeType), preferredColorSpace, outImage);
}

void vkrtFreeLoadedImage(VKRT_LoadedImage* image) {
    if (!image) return;
    free(image->pixels);
    *image = (VKRT_LoadedImage){0};
}
