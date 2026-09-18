#ifndef VKRT_INTEGRATOR_PATH_SURFACE_STATE_SLANG
#define VKRT_INTEGRATOR_PATH_SURFACE_STATE_SLANG

#include "../../geometry/surface/ray_origin.slang"
#include "../../geometry/surface/reconstruct.slang"
#include "../../material/textures.slang"
#include "../../rt/payloads/scene_payload.slang"

struct PathSurfaceState {
    float3 hitPoint = float3(0.0);
    float rayOffset = 0.0;
    SurfaceShadingData surface = SurfaceShadingData();
    Material material;
    ShadingBasis basis;

    __init(SceneRayPayload payload, RayDesc ray) {
        hitPoint = payload.position;
        rayOffset = payload.rayOffset;
        surface = reconstructSurfaceShading(
            meshInfos[payload.instanceIndex],
            payload.primitiveIndex,
            payload.barycentrics,
            ray.Direction
        );
        surface.frontFace = dot(payload.normal, ray.Direction) < 0.0 ? 1u : 0u;
        surface.geometricNormal = surface.frontFace != 0u ? payload.normal : -payload.normal;
        material = materials[surface.materialIndex];

        ShadingBasis unperturbedBasis = makeShadingBasis(surface.shadingNormal, surface.tangent);
        surface.shadingNormal = applyNormalTexture(material, surface.textureData, unperturbedBasis);
        surface.shadingNormal = sanitizeShadingNormal(surface.shadingNormal, surface.geometricNormal, -ray.Direction);
        basis = makeShadingBasis(surface.shadingNormal, surface.tangent);

        applySurfaceTextures(material, surface.textureData);
    }
};

#endif
