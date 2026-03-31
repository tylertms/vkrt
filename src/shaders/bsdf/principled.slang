#ifndef VKRT_BSDF_MODEL_SLANG
#define VKRT_BSDF_MODEL_SLANG

#include "./base.slang"
#include "./lobes/clearcoat.slang"
#include "./lobes/dielectric/eval_rgb.slang"
#include "./lobes/dielectric/eval_spectral.slang"
#include "./lobes/dielectric/interface.slang"
#include "./lobes/dielectric/sample.slang"
#include "./lobes/dielectric/types.slang"
#include "./lobes/ggx.slang"
#include "./lobes/lambertian.slang"
#include "./lobes/oren_nayar.slang"
#include "./lobes/sheen.slang"
#include "./lobes/subsurface.slang"
#include "./principled/attenuation.slang"
#include "./principled/denoiser.slang"
#include "./principled/eval_rgb.slang"
#include "./principled/eval_spectral.slang"
#include "./principled/sample.slang"
#include "./principled/types.slang"
#include "./principled/weights.slang"

// Layered Principled BSDF:
// Sheen on top, then clearcoat, then the base dielectric/metal/substrate stack.
//
// References:
// Blender Principled node definition - https://github.com/blender/blender/blob/main/source/blender/nodes/shader/nodes/node_shader_bsdf_principled.cc
// Blender Cycles closure layering - https://github.com/blender/blender/blob/main/intern/cycles/kernel/svm/closure.h

#endif
