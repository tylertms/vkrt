#ifndef VKRT_LIGHT_ENVIRONMENT_SLANG
#define VKRT_LIGHT_ENVIRONMENT_SLANG

#include "../bsdf/base.slang"
#include "../scene/resources.slang"

static const uint kEnvironmentSamplerRepeatUClampV = 1u;

float2 environmentLatLongUV(float3 worldDir) {
    float3 dir = normalize(worldDir);
    float phi = atan2(dir.y, dir.x) + scene.environmentRotation * (VKRT_PI / 180.0);
    float theta = acos(clamp(dir.z, -1.0, 1.0));
    return float2(frac(phi * (0.5 * VKRT_INV_PI) + 0.5), theta * VKRT_INV_PI);
}

float3 sampleEnvironmentRadiance(float3 worldDir) {
    if (scene.environmentTextureIndex == VKRT_INVALID_INDEX) {
        return scene.environmentLight.xyz;
    }

    uint textureIndex = NonUniformResourceIndex(scene.environmentTextureIndex);
    float3 radiance =
        sceneTextures[textureIndex]
            .SampleLevel(textureSamplers[kEnvironmentSamplerRepeatUClampV], environmentLatLongUV(worldDir), 0.0)
            .xyz;
    return radiance * scene.environmentLight.w;
}

#endif
