#ifndef VKRT_INTEGRATOR_PATH_STATE_SLANG
#define VKRT_INTEGRATOR_PATH_STATE_SLANG

#include "../../bsdf/base.slang"
#include "../../camera/ray.slang"
#include "../../sampling/random.slang"
#include "../../sampling/wavelength.slang"
#include "../../scene/resources.slang"

static const uint VKRT_SCENE_SER_HINT_BITS = 5u;

static const uint VKRT_RAYGEN_MODE_FLAG_BSDF_ONLY_DEBUG = 1u << 0;
static const uint VKRT_RAYGEN_MODE_FLAG_NEE_ONLY_DEBUG = 1u << 1;
static const uint VKRT_RAYGEN_MODE_FLAG_BOUNCE_COUNT_DEBUG = 1u << 2;
static const uint VKRT_RAYGEN_MODE_FLAG_DENOISER_ALBEDO_DEBUG = 1u << 3;
static const uint VKRT_RAYGEN_MODE_FLAG_DENOISER_NORMAL_DEBUG = 1u << 4;
static const uint VKRT_RAYGEN_MODE_FLAG_DENOISER_FEATURE_VALIDITY_DEBUG = 1u << 5;
static const uint VKRT_RAYGEN_MODE_FLAG_DENOISER_FEATURE_DEPTH_DEBUG = 1u << 6;
static const uint VKRT_RAYGEN_MODE_FLAG_DENOISER_FOLLOW_SPECULAR_DEBUG = 1u << 7;
static const uint VKRT_RAYGEN_MODE_FLAG_NEE_ENABLED = 1u << 8;

static const uint VKRT_RAYGEN_PIXEL_FLAG_CAPTURE_SELECTION = 1u << 0;
static const uint VKRT_RAYGEN_FRAME_FLAG_DEBUG_EARLY_OUT = 1u << 0;
static const uint VKRT_RAYGEN_SAMPLE_FLAG_FEATURES_RESOLVED = 1u << 0;
static const uint VKRT_PATH_FLAG_PREV_VERTEX_NEE_ALLOWED = 1u << 0;
static const uint VKRT_HERO_PATH_FLAG_ACTIVE = 1u << 0;

struct RaygenModeState {
    uint debugMode;
    uint flags;

    __init() {
        debugMode = scene.debugMode;
        flags = 0u;

        if (debugMode == VKRT_DEBUG_MODE_BSDF_ONLY) flags |= VKRT_RAYGEN_MODE_FLAG_BSDF_ONLY_DEBUG;
        if (debugMode == VKRT_DEBUG_MODE_NEE_ONLY) flags |= VKRT_RAYGEN_MODE_FLAG_NEE_ONLY_DEBUG;
        if (debugMode == VKRT_DEBUG_MODE_BOUNCE_COUNT) flags |= VKRT_RAYGEN_MODE_FLAG_BOUNCE_COUNT_DEBUG;
        if (debugMode == VKRT_DEBUG_MODE_DENOISER_ALBEDO) flags |= VKRT_RAYGEN_MODE_FLAG_DENOISER_ALBEDO_DEBUG;
        if (debugMode == VKRT_DEBUG_MODE_DENOISER_NORMAL) flags |= VKRT_RAYGEN_MODE_FLAG_DENOISER_NORMAL_DEBUG;
        if (debugMode == VKRT_DEBUG_MODE_DENOISER_FEATURE_VALIDITY) {
            flags |= VKRT_RAYGEN_MODE_FLAG_DENOISER_FEATURE_VALIDITY_DEBUG;
        }
        if (debugMode == VKRT_DEBUG_MODE_DENOISER_FEATURE_DEPTH) {
            flags |= VKRT_RAYGEN_MODE_FLAG_DENOISER_FEATURE_DEPTH_DEBUG;
        }
        if (debugMode == VKRT_DEBUG_MODE_DENOISER_FOLLOW_SPECULAR) {
            flags |= VKRT_RAYGEN_MODE_FLAG_DENOISER_FOLLOW_SPECULAR_DEBUG;
        }
        if (scene.misNeeEnabled != 0u && scene.emissiveMeshCount > 0u) {
            flags |= VKRT_RAYGEN_MODE_FLAG_NEE_ENABLED;
        }
    }
};

bool raygenModeHas(RaygenModeState modeState, uint flag) {
    return (modeState.flags & flag) != 0u;
}

struct RaygenPixelState {
    int2 pixel;
    uint spp;
    float4 previousAccumulation;
    uint previousSamples;
    uint flags;
    uint firstHitInstance = VKRT_INVALID_INDEX;

    __init(int2 pixel) {
        this.pixel = pixel;
        spp = max(scene.samplesPerPixel, 1u);
        previousAccumulation = accumulationReadImage[pixel];
        previousSamples = (uint)(previousAccumulation.w + 0.5);
        flags = 0u;

        int2 selectionPixel = int2(int(selection[0].pixel & 0xffffu), int((selection[0].pixel >> 16) & 0xffffu));
        if (all(pixel == selectionPixel)) {
            flags |= VKRT_RAYGEN_PIXEL_FLAG_CAPTURE_SELECTION;
        }
    }
};

bool raygenPixelCapturesSelection(RaygenPixelState pixelState) {
    return (pixelState.flags & VKRT_RAYGEN_PIXEL_FLAG_CAPTURE_SELECTION) != 0u;
}

struct RaygenFrameState {
    float3 radiance = float3(0.0);
    float3 albedo = float3(0.0);
    float3 normal = float3(0.0);
    float featureWeight = 0.0;
    float featureDepth = 0.0;
    float followSpecular = 0.0;
    uint flags = 0u;
};

bool raygenFrameDebugEarlyOut(RaygenFrameState frameState) {
    return (frameState.flags & VKRT_RAYGEN_FRAME_FLAG_DEBUG_EARLY_OUT) != 0u;
}

[mutating] void setRaygenFrameDebugEarlyOut(inout RaygenFrameState frameState) {
    frameState.flags |= VKRT_RAYGEN_FRAME_FLAG_DEBUG_EARLY_OUT;
}

struct RgbSampleState {
    float3 radiance = float3(0.0);
    float3 albedo = float3(0.0);
    float3 normal = float3(0.0);
    float featureWeight = 0.0;
    float featureDepth = 0.0;
    float followSpecular = 0.0;
    uint flags = 0u;
};

struct SpectralSingleSampleState {
    float spectralRadiance = 0.0;
    float3 albedo = float3(0.0);
    float3 normal = float3(0.0);
    float featureWeight = 0.0;
    float featureDepth = 0.0;
    float followSpecular = 0.0;
    uint flags = 0u;
};

struct SpectralHeroSampleState {
    float3 radiance = float3(0.0);
    float spectralRadianceScalar = 0.0;
    float4 spectralRadiance = float4(0.0);
    float3 albedo = float3(0.0);
    float3 normal = float3(0.0);
    float featureWeight = 0.0;
    float featureDepth = 0.0;
    float followSpecular = 0.0;
    uint flags = 0u;
};

bool raygenSampleFeaturesResolved(uint flags) {
    return (flags & VKRT_RAYGEN_SAMPLE_FLAG_FEATURES_RESOLVED) != 0u;
}

[mutating] void setRaygenSampleFeaturesResolved(inout uint flags) {
    flags |= VKRT_RAYGEN_SAMPLE_FLAG_FEATURES_RESOLVED;
}

struct PathCommonState {
    uint rng = 0u;
    MediumState medium = MediumState();
    uint flags = 0u;
    float prevBsdfPdf = 0.0;
    uint bounceCount = 0u;
    RayDesc ray;

    [mutating] void initCommon(RaygenPixelState pixelState, uint sampleIndex) {
        rng = initPixelSeed(pixelState.pixel, scene.frameNumber, pixelState.previousSamples + sampleIndex);
        medium = MediumState();

        float2 jitter = float2(rand(rng), rand(rng)) - float2(0.5);
        ray = makePrimaryRay(pixelState.pixel, jitter);
    }
};

bool pathPrevVertexNeeAllowed(PathCommonState state) {
    return (state.flags & VKRT_PATH_FLAG_PREV_VERTEX_NEE_ALLOWED) != 0u;
}

[mutating] void setPathPrevVertexNeeAllowed(inout PathCommonState state, bool enabled) {
    state.flags = enabled ? (state.flags | VKRT_PATH_FLAG_PREV_VERTEX_NEE_ALLOWED)
                          : (state.flags & ~VKRT_PATH_FLAG_PREV_VERTEX_NEE_ALLOWED);
}

struct RgbPathState {
    PathCommonState common = PathCommonState();
    float3 throughput = float3(1.0);

    __init(RaygenPixelState pixelState, uint sampleIndex) {
        common.initCommon(pixelState, sampleIndex);
    }
};

struct SpectralSinglePathState {
    PathCommonState common = PathCommonState();
    WavelengthSample wavelength = {};
    float spectralThroughput = 1.0;

    __init(RaygenPixelState pixelState, uint sampleIndex) {
        common.initCommon(pixelState, sampleIndex);
        wavelength = sampleUniformWavelength(common.rng, pixelState.previousSamples + sampleIndex);
    }
};

struct SpectralHeroPathState {
    PathCommonState common = PathCommonState();
    WavelengthSample scalarWavelength = {};
    float4 wavelengthsNm = float4(0.0);
    float4 invWavelengthPdf = float4(0.0);
    float spectralThroughputScalar = 1.0;
    float4 spectralThroughput = float4(1.0, 0.0, 0.0, 0.0);
    float4 techniquePathPdf = float4(1.0, 0.0, 0.0, 0.0);
    float4 prevVertexTechniquePathPdf = float4(0.0);
    float4 prevBsdfTechniquePdf = float4(0.0);
    uint flags = 0u;

    __init(RaygenPixelState pixelState, uint sampleIndex) {
        common.initCommon(pixelState, sampleIndex);
        wavelengthsNm = sampleHeroWavelengths4(common.rng, pixelState.previousSamples + sampleIndex);
        invWavelengthPdf = heroWavelengthInvPdf4();
        spectralThroughput = float4(1.0);
        techniquePathPdf = float4(1.0);
        flags = VKRT_HERO_PATH_FLAG_ACTIVE;
    }
};

bool spectralHeroPathActive(SpectralHeroPathState state) {
    return (state.flags & VKRT_HERO_PATH_FLAG_ACTIVE) != 0u;
}

[mutating] void setSpectralHeroPathActive(inout SpectralHeroPathState state, bool enabled) {
    state.flags = enabled ? (state.flags | VKRT_HERO_PATH_FLAG_ACTIVE) : (state.flags & ~VKRT_HERO_PATH_FLAG_ACTIVE);
}

#endif
