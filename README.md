# vkrt

A Vulkan path tracer in C with Slang shaders, for Windows and Linux.
The renderer is available as the `vkrt_core` library with a [C API](src/core/api/vkrt.h).

## Features

### Renderer

- Vulkan hardware-accelerated ray tracing and mesh instancing.
- Progressive accumulation and stochastic anti-aliasing.
- RGB, single-wavelength, and four-wavelength hero spectral path tracing.
- RGB2Spec spectral reconstruction.
- Next-event estimation (NEE) and multiple importance sampling (MIS).
- Emissive mesh lights and HDR environment lighting.
- Layered BSDFs with diffuse, metal, dielectric, transmission, clearcoat, sheen, and subsurface approximation lobes.
- Anisotropic GGX reflection and refraction with spherical-cap VNDF sampling.
- Complex-IOR conductor Fresnel and dielectric Fresnel.
- Total internal reflection and Abbe-number spectral dispersion.
- Beer-Lambert volume absorption.
- PBR textures and tangent-space normal mapping.
- Alpha masking and stochastic transparency.
- ACES filmic tone mapping and automatic exposure.
- Open Image Denoise.

### Editor

- Interactive path-traced viewport with object selection.
- Scene editing with full material and BSDF controls.
- glTF/GLB import with mesh instances, PBR materials, and textures.
- Render mode with EXR, PNG, and JPEG export.
- JSON scene save/load.
- Debug views and command-line rendering.

## BSDFs

| Lobe | Model | References |
| --- | --- | --- |
| Lambertian diffuse | Ideal diffuse reflection. | [PBRT diffuse reflection][pbrt-diffuse] |
| Oren-Nayar diffuse | Oren-Nayar rough diffuse reflection. | [Oren and Nayar 1994][oren-nayar], [PBRT implementation][pbrt-oren] |
| Metal reflection | Anisotropic GGX, spherical-cap VNDF sampling, height-correlated Smith masking, and conductor (eta/k) or Schlick Fresnel. | [PBRT microfacets][pbrt-microfacet], [PBRT Fresnel][pbrt-fresnel], [Schlick 1994][schlick], [VNDF][ggx-heitz], [spherical caps][ggx-caps] |
| Dielectric reflection | Anisotropic GGX, spherical-cap VNDF sampling, height-correlated Smith masking, and dielectric or tinted Schlick Fresnel. | [PBRT dielectric BSDF][pbrt-dielectric], [PBRT Fresnel][pbrt-fresnel], [Disney 2012][disney-2012], [VNDF][ggx-heitz], [spherical caps][ggx-caps] |
| Dielectric transmission | Anisotropic GGX refraction, spherical-cap VNDF sampling, height-correlated Smith masking, total internal reflection, and wavelength-dependent IOR. | [Walter et al. 2007][walter], [PBRT dielectric BSDF][pbrt-dielectric], [Disney 2015][disney-2015], [VNDF][ggx-heitz], [spherical caps][ggx-caps] |
| Clearcoat | Disney GTR1 with Schlick Fresnel and fixed-roughness Smith masking. | [Disney 2012][disney-2012], [reference implementation][disney-code], [PBRT microfacets][pbrt-microfacet] |
| Sheen | LTC fiber sheen. | [Zeltner, Burley, and Chiang 2022][sheen-paper], [Cycles implementation][cycles-sheen], [lookup tables][cycles-tables] |
| Subsurface approximation | Disney local diffuse approximation. | [Disney 2012][disney-2012], [reference implementation][disney-code], [Disney 2015][disney-2015] |

<details>
<summary>Implementation references</summary>

- Material layering: [Disney anisotropy][disney-2012], [Principled BSDF][principled-node], [Cycles closures][cycles-closure], and [Karis directional albedo][karis].
- Spectral rendering: [hero wavelengths][hero], [PBRT sampled spectra][pbrt-spectra], [RGB2Spec][rgb2spec] ([implementation][rgb2spec-code], [API][rgb2spec-api]), and [Abbe dispersion][abbe].
- Color: [CIE XYZ fits][cie] and [BT.709][bt709].
- Sampling: [MIS][mis], [alias tables][alias], and [integer hashing][hash].
- Geometry: [orthonormal bases][basis], [Cycles normal correction][cycles-normal], and [ray-origin error bounds][ray-origin].
- Asset format: [glTF 2.0][gltf].

</details>

## Usage

Download a package from [Releases](https://github.com/tylertms/vkrt/releases).
Extract it and run `bin/vkrt.exe` on Windows or `bin/vkrt` on Linux, or run the Linux AppImage.
Linux packages require glibc 2.39 or newer (Ubuntu 24.04 or newer).

Open a scene with **File > Open**.
Set the output size and sample count, then select **Start Render**.
After the render completes, use **File > Save Render** to export the image.
Example scenes are in `assets/scenes`.

Run `vkrt --help` for command-line options.

## Build

- GPU with Vulkan ray tracing support and a current graphics driver.
- Git and Python 3.12 or newer.
- [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) 1.4.357 or newer, with its environment configured and `slangc` on `PATH`.
- Windows: Visual Studio Build Tools with the C++ desktop workload.

```sh
git clone https://github.com/tylertms/vkrt
cd vkrt
```

### Windows (x64 developer PowerShell)

```powershell
python -m venv .venv
./.venv/Scripts/Activate.ps1
python -m pip install -r scripts/requirements-build.txt
meson setup build --buildtype=release -Db_vscrt=static_from_buildtype
meson compile -C build
./build/vkrt.exe
```

### Linux (Ubuntu 24.04)

```sh
sudo apt-get update
sudo apt-get install build-essential pkg-config nasm python3-venv libvulkan1 libdbus-1-dev libwayland-dev libxkbcommon-dev wayland-protocols libx11-dev libxrandr-dev libxinerama-dev libxi-dev libxcursor-dev libxext-dev
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r scripts/requirements-build.txt
meson setup build --buildtype=release -Dfile_dialogs=enabled
meson compile -C build
./build/vkrt
```

## Development

```sh
python -m pip install -r scripts/requirements-lint.txt
pre-commit install
pre-commit run --all-files
```

## Dependencies

| Library | Version |
| --- | --- |
| GLFW | 3.5.1 |
| zlib | 1.3.2 |
| libspng | 0.7.4 |
| libjpeg-turbo | 3.2.0 |
| Open Image Denoise | 2.5.1 |
| Dear ImGui | 1.92.9b-docking |
| Dear Bindings | 0.21, for ImGui 1.92.9b-docking |
| cglm | 0.9.6 |
| cgltf | 1.15 |
| cJSON | 1.7.19 |
| TinyEXR | 3.2.0 |
| Native File Dialog Extended | 1.4.0 |

## Gallery

<img width="1080" height="607" alt="image" src="https://github.com/user-attachments/assets/17a43e29-341f-4718-82ba-5b4ec32b923c" />

[pbrt-diffuse]: https://www.pbr-book.org/4ed/Reflection_Models/Diffuse_Reflection
[oren-nayar]: https://www.cs.columbia.edu/CAVE/publications/pdfs/Oren_SIGGRAPH94.pdf
[pbrt-oren]: https://www.pbr-book.org/3ed-2018/Reflection_Models/Microfacet_Models
[pbrt-microfacet]: https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory
[pbrt-fresnel]: https://www.pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission
[pbrt-dielectric]: https://www.pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF
[schlick]: https://diglib.eg.org/items/581037f7-1e5c-4859-a107-6dfeeb8f3ccb
[disney-2012]: https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
[disney-2015]: https://blog.selfshadow.com/publications/s2015-shading-course/burley/s2015_pbs_disney_bsdf_notes.pdf
[disney-code]: https://github.com/wdas/brdf/blob/main/src/brdfs/disney.brdf
[walter]: https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf
[sheen-paper]: https://tizianzeltner.com/projects/Zeltner2022Practical/
[cycles-sheen]: https://github.com/blender/blender/blob/main/intern/cycles/kernel/closure/bsdf_sheen.h
[cycles-tables]: https://github.com/blender/blender/blob/main/intern/cycles/scene/shader.tables
[ggx-caps]: https://arxiv.org/abs/2306.05044
[ggx-heitz]: https://jcgt.org/published/0007/04/01/paper.pdf
[principled-node]: https://github.com/blender/blender/blob/main/source/blender/nodes/shader/nodes/node_shader_bsdf_principled.cc
[cycles-closure]: https://github.com/blender/blender/blob/main/intern/cycles/kernel/svm/closure.h
[karis]: https://www.unrealengine.com/blog/physically-based-shading-on-mobile
[hero]: https://cgg.mff.cuni.cz/publications/hero-wavelength-spectral-sampling/
[pbrt-spectra]: https://www.pbr-book.org/4ed/Radiometry%2C_Spectra%2C_and_Color/Representing_Spectral_Distributions
[rgb2spec]: https://rgl.epfl.ch/publications/Jakob2019Spectral
[rgb2spec-code]: https://github.com/mitsuba-renderer/rgb2spec/blob/master/rgb2spec.c
[rgb2spec-api]: https://github.com/mitsuba-renderer/rgb2spec/blob/master/rgb2spec.h
[cie]: https://jcgt.org/published/0002/02/01/
[bt709]: https://www.itu.int/rec/R-REC-BT.709
[abbe]: https://en.wikipedia.org/wiki/Abbe_number
[mis]: https://www.pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency
[alias]: https://www.keithschwarz.com/darts-dice-coins/
[hash]: https://nullprogram.com/blog/2018/07/31/
[basis]: https://jcgt.org/published/0006/01/01/paper-lowres.pdf
[cycles-normal]: https://github.com/blender/blender/blob/v4.3.0/intern/cycles/kernel/closure/bsdf_util.h#L129-L213
[ray-origin]: https://developer.nvidia.com/blog/solving-self-intersection-artifacts-in-directx-raytracing/
[gltf]: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
