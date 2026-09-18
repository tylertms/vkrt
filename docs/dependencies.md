# Dependency inventory

Upstream release data was checked on 2026-09-17.
Stable releases take priority over rolling development builds.

| Dependency | Pinned version | Source |
| --- | --- | --- |
| Vulkan SDK, Windows | 1.4.357.0 | [LunarG](https://vulkan.lunarg.com/sdk/home) |
| Vulkan SDK, Linux | 1.4.357.1 | [LunarG](https://vulkan.lunarg.com/sdk/home) |
| GLFW | 3.5.1 | [GLFW releases](https://github.com/glfw/glfw/releases) |
| zlib | 1.3.2 | [zlib releases](https://github.com/madler/zlib/releases) |
| libspng | 0.7.4 | [libspng releases](https://github.com/randy408/libspng/releases) |
| libjpeg-turbo | 3.2.0 | [libjpeg-turbo releases](https://github.com/libjpeg-turbo/libjpeg-turbo/releases) |
| Open Image Denoise | 2.5.1 | [OIDN releases](https://github.com/RenderKit/oidn/releases) |
| Dear ImGui | 1.92.9b-docking | [ImGui releases](https://github.com/ocornut/imgui/releases) |
| Dear Bindings | 0.21, ImGui 1.92.9b-docking | [Dear Bindings releases](https://github.com/dearimgui/dear_bindings/releases) |
| cglm | 0.9.6 | [cglm releases](https://github.com/recp/cglm/releases) |
| cgltf | 1.15 | [cgltf releases](https://github.com/jkuhlmann/cgltf/releases) |
| cJSON | 1.7.19 | [cJSON releases](https://github.com/DaveGamble/cJSON/releases) |
| TinyEXR | 3.2.0 | [TinyEXR releases](https://github.com/syoyo/tinyexr/releases) |
| Native File Dialog Extended | 1.4.0 | [NFD releases](https://github.com/btzy/nativefiledialog-extended/releases) |
| stb | `2c980bb59875b0d32144a71867fbdebb2f77cd20` | [stb](https://github.com/nothings/stb) |
| rgb2spec provenance and license | `721145dedf2491851bd46ab8fd165955cb38ddaf` | [rgb2spec](https://github.com/mitsuba-renderer/rgb2spec) |
| Meson | 1.12.0 | [Meson releases](https://github.com/mesonbuild/meson/releases) |
| Ninja | 1.13.2 | [Ninja releases](https://github.com/ninja-build/ninja/releases) |
| CMake | 4.4.3 | [CMake releases](https://github.com/Kitware/CMake/releases) |
| clang-tidy | 22.1.8 | [Python distribution](https://pypi.org/project/clang-tidy/) |
| Ruff | 0.16.8 | [Python distribution](https://pypi.org/project/ruff/) |
| appimagetool | 1.9.1 | [appimagetool releases](https://github.com/AppImage/appimagetool/releases) |
| AppImage runtime | 20251108 | [Tagged runtime](https://github.com/AppImage/type2-runtime/releases/tag/20251108) |

The SDK supplies Slang. The SDK archive checksum pins its compiler and runtime libraries together.
The AppImage runtime uses the latest dated release, rather than the mutable `continuous` asset.
Font files, scene assets, and the existing rgb2spec coefficient table are application data.

## GitHub Actions

The workflows pin these releases to their resolved commit hashes:

| Action | Release |
| --- | --- |
| actions/checkout | v7.0.1 |
| actions/setup-python | v7.0.0 |
| actions/cache | v6.1.0 |
| actions/upload-artifact | v7.0.1 |
| actions/download-artifact | v8.0.1 |
| ilammy/msvc-dev-cmd | v1.13.0 |
| softprops/action-gh-release | v3.0.3 |
