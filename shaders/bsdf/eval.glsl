#ifndef BSDF_EVALUATE
#define BSDF_EVALUATE

#include "diffuse.glsl"
#include "specular.glsl"

void evalBSDF(Payload payload, Material material, inout Ray ray, inout vec3 radiance, inout vec3 throughput, inout uint state) {
    vec3 diffuseDir = diffuseBSDF(payload.normal, state);
    vec3 specularDir = specularBSDF(payload.normal, ray.dir, state);
    uint specular = material.specular > rand(state) ? 1 : 0;

    ray.dir = normalize(mix(diffuseDir, specularDir, clamp(1.0 - material.roughness + specular, 0.0, 1.0)));
    ray.origin = payload.point;

    vec3 emitted = material.emissionColor * material.emissionStrength;
    radiance += throughput * emitted;
    if (specular == 0) throughput *= material.baseColor;
}

#endif
