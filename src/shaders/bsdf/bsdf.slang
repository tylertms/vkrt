#ifndef VKRT_BSDF_BSDF_SLANG
#define VKRT_BSDF_BSDF_SLANG

#include "common.slang"
#include "lambertian.slang"

BSDFSample sampleBSDF(BSDFInput input, inout uint rng) {
    return sampleLambertian(input, rng);
}

#endif
