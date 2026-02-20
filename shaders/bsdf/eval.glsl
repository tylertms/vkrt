#ifndef BSDF_EVALUATE
#define BSDF_EVALUATE

#include "diffuse.glsl"
#include "specular.glsl"

void evalBSDF(Payload payload, Material material, inout Ray ray, inout vec3 radiance, inout vec3 throughput, inout uint state) {
    vec3 diffuseDir = diffuseBSDF(payload.normal, state);
    vec3 outDir = diffuseDir;

    ray.origin = payload.point;
    ray.dir = outDir;

    vec3 emitted = material.emissionColor * material.emissionStrength;
    radiance += throughput * emitted;
    throughput *= material.baseColor;
}

#endif
