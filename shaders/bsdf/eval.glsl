#ifndef BSDF_EVALUATE
#define BSDF_EVALUATE

#include "diffuse.glsl"
#include "specular.glsl"

void evalBSDF(Payload payload, inout Ray ray, inout vec3 radiance, inout vec3 throughput, inout uint state) {
    vec3 diffuseDir = diffuseBSDF(payload.normal, state);
    vec3 outDir = diffuseDir;

    ray.origin = payload.point;
    ray.dir = outDir;

    vec3 emitted = payload.material.emissionColor * payload.material.emissionStrength;
    radiance += throughput * emitted;
    throughput *= payload.material.baseColor;
}

#endif
