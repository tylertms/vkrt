#include "exr.h"

#include "constants.h"
#include "debug.h"
#include "formats.h"
#include "image.h"
#include "io.h"

#include <cstdint>
#include <zlib.h>

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_IMPLEMENTATION
#ifdef _MSC_VER
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#pragma warning(push)
#pragma warning(disable : 4245 4702)
#endif
#include "tinyexr.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

constexpr int kTinyexrZlibStatusOk = Z_OK;

void freeTinyEXRError(const char* error) {
    if (error != nullptr) {
        FreeEXRErrorMessage(error);
    }
}

int channelNameMatches(const char* channelName, const char* componentName) {
    if ((channelName == nullptr) || (componentName == nullptr)) return 0;
    if (std::strcmp(channelName, componentName) == 0) return 1;

    const char* suffix = std::strrchr(channelName, '.');
    return static_cast<int>((static_cast<int>(suffix != nullptr) != 0) && std::strcmp(suffix + 1, componentName) == 0);
}

int queryChannelIndex(const EXRHeader& header, const char* componentName) {
    for (int i = 0; i < header.num_channels; i++) {
        if (channelNameMatches(header.channels[i].name, componentName) != 0) {
            return i;
        }
    }
    return -1;
}

int queryLoadFormat(const EXRHeader& header) {
    for (int i = 0; i < header.num_channels; i++) {
        if (header.pixel_types[i] != TINYEXR_PIXELTYPE_HALF) {
            return VKRT_TEXTURE_FORMAT_RGBA32_SFLOAT;
        }
    }
    return VKRT_TEXTURE_FORMAT_RGBA16_SFLOAT;
}

int configureRequestedPixelTypes(EXRHeader* header, uint32_t format) {
    if ((header == nullptr) || (header->requested_pixel_types == nullptr)) return 0;
    for (int i = 0; i < header->num_channels; i++) {
        header->requested_pixel_types[i] =
            format == VKRT_TEXTURE_FORMAT_RGBA16_SFLOAT ? TINYEXR_PIXELTYPE_HALF : TINYEXR_PIXELTYPE_FLOAT;
    }
    return 1;
}

int finalizeHalfImage(
    const EXRHeader& header,
    const EXRImage& image,
    const char* sourceLabel,
    VKRT_LoadedImage* outImage
) {
    const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    if (pixelCount > std::numeric_limits<size_t>::max() / 4u) {
        LOG_ERROR("EXR image dimensions overflow for %s", sourceLabel);
        return 0;
    }

    int const indexR = queryChannelIndex(header, "R");
    int const indexG = queryChannelIndex(header, "G");
    int const indexB = queryChannelIndex(header, "B");
    int const indexA = queryChannelIndex(header, "A");
    int const indexY = queryChannelIndex(header, "Y");
    if ((indexR < 0 || indexG < 0 || indexB < 0) && indexY < 0) {
        LOG_ERROR("EXR image from %s did not contain RGB(A) or Y channels", sourceLabel);
        return 0;
    }

    uint16_t* pixels = static_cast<uint16_t*>(std::malloc(pixelCount * 4u * sizeof(uint16_t)));
    if (pixels == nullptr) {
        LOG_ERROR("Failed to allocate EXR decode buffer for %s", sourceLabel);
        return 0;
    }

    const uint16_t* channelR = indexR >= 0 ? reinterpret_cast<const uint16_t*>(image.images[indexR]) : nullptr;
    const uint16_t* channelG = indexG >= 0 ? reinterpret_cast<const uint16_t*>(image.images[indexG]) : nullptr;
    const uint16_t* channelB = indexB >= 0 ? reinterpret_cast<const uint16_t*>(image.images[indexB]) : nullptr;
    const uint16_t* channelA = indexA >= 0 ? reinterpret_cast<const uint16_t*>(image.images[indexA]) : nullptr;
    const uint16_t* channelY = indexY >= 0 ? reinterpret_cast<const uint16_t*>(image.images[indexY]) : nullptr;

    for (size_t i = 0; i < pixelCount; i++) {
        pixels[(i * 4u) + 0u] = (channelR != nullptr) ? channelR[i] : channelY[i];
        pixels[(i * 4u) + 1u] = (channelG != nullptr) ? channelG[i] : channelY[i];
        pixels[(i * 4u) + 2u] = (channelB != nullptr) ? channelB[i] : channelY[i];
        pixels[(i * 4u) + 3u] = (channelA != nullptr) ? channelA[i] : 0x3c00u;
    }

    outImage->pixels = pixels;
    outImage->width = static_cast<uint32_t>(image.width);
    outImage->height = static_cast<uint32_t>(image.height);
    outImage->format = VKRT_TEXTURE_FORMAT_RGBA16_SFLOAT;
    outImage->colorSpace = VKRT_TEXTURE_COLOR_SPACE_LINEAR;
    return 1;
}

int finalizeFloatImage(
    const EXRHeader& header,
    const EXRImage& image,
    const char* sourceLabel,
    VKRT_LoadedImage* outImage
) {
    const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    if (pixelCount > std::numeric_limits<size_t>::max() / 4u) {
        LOG_ERROR("EXR image dimensions overflow for %s", sourceLabel);
        return 0;
    }

    int const indexR = queryChannelIndex(header, "R");
    int const indexG = queryChannelIndex(header, "G");
    int const indexB = queryChannelIndex(header, "B");
    int const indexA = queryChannelIndex(header, "A");
    int const indexY = queryChannelIndex(header, "Y");
    if ((indexR < 0 || indexG < 0 || indexB < 0) && indexY < 0) {
        LOG_ERROR("EXR image from %s did not contain RGB(A) or Y channels", sourceLabel);
        return 0;
    }

    float* pixels = static_cast<float*>(std::malloc(pixelCount * 4u * sizeof(float)));
    if (pixels == nullptr) {
        LOG_ERROR("Failed to allocate EXR decode buffer for %s", sourceLabel);
        return 0;
    }

    const float* channelR = indexR >= 0 ? reinterpret_cast<const float*>(image.images[indexR]) : nullptr;
    const float* channelG = indexG >= 0 ? reinterpret_cast<const float*>(image.images[indexG]) : nullptr;
    const float* channelB = indexB >= 0 ? reinterpret_cast<const float*>(image.images[indexB]) : nullptr;
    const float* channelA = indexA >= 0 ? reinterpret_cast<const float*>(image.images[indexA]) : nullptr;
    const float* channelY = indexY >= 0 ? reinterpret_cast<const float*>(image.images[indexY]) : nullptr;

    for (size_t i = 0; i < pixelCount; i++) {
        pixels[(i * 4u) + 0u] = (channelR != nullptr) ? channelR[i] : channelY[i];
        pixels[(i * 4u) + 1u] = (channelG != nullptr) ? channelG[i] : channelY[i];
        pixels[(i * 4u) + 2u] = (channelB != nullptr) ? channelB[i] : channelY[i];
        pixels[(i * 4u) + 3u] = (channelA != nullptr) ? channelA[i] : 1.0f;
    }

    outImage->pixels = pixels;
    outImage->width = static_cast<uint32_t>(image.width);
    outImage->height = static_cast<uint32_t>(image.height);
    outImage->format = VKRT_TEXTURE_FORMAT_RGBA32_SFLOAT;
    outImage->colorSpace = VKRT_TEXTURE_COLOR_SPACE_LINEAR;
    return 1;
}

template <typename ParseVersionFn, typename ParseHeaderFn, typename LoadImageFn>
int loadEXRImageCommon(
    ParseVersionFn parseVersion,
    ParseHeaderFn parseHeader,
    LoadImageFn loadImage,
    const char* sourceLabel,
    VKRT_LoadedImage* outImage
) {
    if (!sourceLabel || !outImage) return 0;

    EXRVersion version;
    if (parseVersion(&version) != TINYEXR_SUCCESS) {
        LOG_ERROR("Invalid EXR file: %s", sourceLabel);
        return 0;
    }

    EXRHeader header;
    InitEXRHeader(&header);
    const char* error = nullptr;
    int result = parseHeader(&header, &version, &error);
    if (result != TINYEXR_SUCCESS) {
        LOG_ERROR("EXR header decode from %s failed (%s)", sourceLabel, error ? error : "unknown error");
        freeTinyEXRError(error);
        FreeEXRHeader(&header);
        return 0;
    }

    outImage->pixels = nullptr;
    outImage->width = 0u;
    outImage->height = 0u;
    outImage->format = VKRT_TEXTURE_FORMAT_RGBA32_SFLOAT;
    outImage->colorSpace = VKRT_TEXTURE_COLOR_SPACE_LINEAR;

    uint32_t const format = static_cast<uint32_t>(queryLoadFormat(header));
    if (!configureRequestedPixelTypes(&header, format)) {
        FreeEXRHeader(&header);
        return 0;
    }

    EXRImage image;
    InitEXRImage(&image);
    result = loadImage(&image, &header, &error);
    if (result != TINYEXR_SUCCESS) {
        LOG_ERROR("EXR decode from %s failed (%s)", sourceLabel, error ? error : "unknown error");
        freeTinyEXRError(error);
        FreeEXRImage(&image);
        FreeEXRHeader(&header);
        return 0;
    }

    int const ok = format == VKRT_TEXTURE_FORMAT_RGBA16_SFLOAT
                     ? finalizeHalfImage(header, image, sourceLabel, outImage)
                     : finalizeFloatImage(header, image, sourceLabel, outImage);

    FreeEXRImage(&image);
    FreeEXRHeader(&header);
    freeTinyEXRError(error);
    return ok;
}

}  // namespace

extern "C" {

int vkrtLoadEXRImageFromFile(const char* path, VKRT_LoadedImage* outImage) {
    size_t encodedSize = 0u;
    const char* encodedBytes = nullptr;

    if ((path == nullptr) || (path[0] == 0) || (outImage == nullptr)) {
        return 0;
    }
    *outImage = VKRT_LoadedImage{};
    encodedBytes = readFile(path, &encodedSize);
    if (encodedBytes == nullptr) {
        return 0;
    }

    int const ok = loadEXRImageCommon(
        [&](EXRVersion* version) {
            (void)kTinyexrZlibStatusOk;
            return ParseEXRVersionFromMemory(version, reinterpret_cast<const unsigned char*>(encodedBytes), encodedSize);
        },
        [&](EXRHeader* header, const EXRVersion* version, const char** error) {
            return ParseEXRHeaderFromMemory(
                header,
                version,
                reinterpret_cast<const unsigned char*>(encodedBytes),
                encodedSize,
                error
            );
        },
        [&](EXRImage* image, const EXRHeader* header, const char** error) {
            return LoadEXRImageFromMemory(
                image,
                header,
                reinterpret_cast<const unsigned char*>(encodedBytes),
                encodedSize,
                error
            );
        },
        path,
        outImage
    );
    free((void*)encodedBytes);
    return ok;
}

int vkrtLoadEXRImageFromMemory(const void* data, size_t size, const char* sourceLabel, VKRT_LoadedImage* outImage) {
    if ((data == nullptr) || size == 0u || (sourceLabel == nullptr) || (outImage == nullptr)) {
        return 0;
    }
    *outImage = VKRT_LoadedImage{};

    return loadEXRImageCommon(
        [&](EXRVersion* version) {
            return ParseEXRVersionFromMemory(version, static_cast<const unsigned char*>(data), size);
        },
        [&](EXRHeader* header, const EXRVersion* version, const char** error) {
            return ParseEXRHeaderFromMemory(header, version, static_cast<const unsigned char*>(data), size, error);
        },
        [&](EXRImage* image, const EXRHeader* header, const char** error) {
            return LoadEXRImageFromMemory(image, header, static_cast<const unsigned char*>(data), size, error);
        },
        sourceLabel,
        outImage
    );
}

int vkrtWriteEXRFromRGBA32F(const char* path, const float* rgba32f, uint32_t width, uint32_t height) {
    if ((path == nullptr) || (path[0] == 0) || (rgba32f == nullptr) || width == 0u || height == 0u) {
        return 0;
    }
    if (width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        LOG_ERROR("EXR export dimensions exceed codec limits: %ux%u", width, height);
        return 0;
    }

    const char* error = nullptr;
    int const result = SaveEXR(rgba32f, static_cast<int>(width), static_cast<int>(height), 4, 0, path, &error);
    if (result != TINYEXR_SUCCESS) {
        LOG_ERROR("EXR export failed for '%s' (%s)", path, error ? error : "unknown error");
        freeTinyEXRError(error);
        return 0;
    }

    freeTinyEXRError(error);
    return 1;
}

}  // extern "C"
