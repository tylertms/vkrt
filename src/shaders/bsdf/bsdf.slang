#ifndef VKRT_BSDF_BSDF_SLANG
#define VKRT_BSDF_BSDF_SLANG

#include "common.slang"
#include "lambertian.slang"

BSDFSample sampleBSDF(BSDFInput input, inout uint state) {
    return sampleLambertian(input, state);
}

#endif
