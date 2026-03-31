#ifndef VKRT_BSDF_COMMON_SLANG
#define VKRT_BSDF_COMMON_SLANG

#include "../camera/ray.slang"
#include "../sampling/random.slang"
#include "../utility/color.slang"
#include "../utility/spectral.slang"
#include "./base/basis.slang"
#include "./base/fresnel.slang"
#include "./base/interface.slang"
#include "./base/material.slang"
#include "./base/math.slang"
#include "./base/medium.slang"
#include "./types.slang"

// References:
// Basis construction: Duff et al., 2017 - https://graphics.pixar.com/library/OrthonormalB/paper.pdf
// Diffuse color workflow: Khronos Group, 2017 - https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
// Luminance weights: ITU-R BT.709, 2015 - https://www.itu.int/rec/R-REC-BT.709
// Disney BRDF: Burley, 2012 - https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
// Schlick Fresnel: Schlick, 1994 - https://diglib.eg.org/items/581037f7-1e5c-4859-a107-6dfeeb8f3ccb
// Dielectric/conductor Fresnel: Pharr, Jakob, Humphreys, 2023 - https://www.pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission

#endif
