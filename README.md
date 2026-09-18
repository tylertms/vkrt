# vkrt

Hardware path tracing in C with Vulkan, for Windows and Linux.
Requires a GPU with Vulkan ray tracing support and a current graphics driver.

## Run

Download a package from [Releases](https://github.com/tylertms/vkrt/releases).
Extract the archive and run `bin/vkrt.exe` on Windows or `bin/vkrt` on Linux.
The Linux AppImage runs directly. Linux packages require glibc 2.39 or newer (Ubuntu 24.04 or newer).

OIDN denoising uses a supported GPU, with a CPU fallback.
Both use high-quality filtering with albedo and normal inputs.

## Build

Install Git, Python 3.12 or newer, and the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) 1.4.357 or newer.
The SDK includes Slang. `VULKAN_SDK` must point to the SDK, and `slangc` must be on `PATH`.

```sh
git clone https://github.com/tylertms/vkrt
cd vkrt
```

### Windows

Install Visual Studio Build Tools with the C++ desktop workload.
Open an x64 developer PowerShell prompt in the repository:

```powershell
python -m venv .venv
./.venv/Scripts/Activate.ps1
python -m pip install -r scripts/requirements-build.txt
$env:PATH = "$env:VULKAN_SDK/Bin;$env:PATH"
meson setup build --buildtype=release -Db_vscrt=static_from_buildtype
meson compile -C build
./build/vkrt.exe
```

### Linux

On Ubuntu 24.04, install the development packages and load the SDK environment.
Replace the SDK path with your extracted SDK directory.

```sh
sudo apt-get update
sudo apt-get install build-essential pkg-config nasm python3-venv libvulkan1 libdbus-1-dev libwayland-dev libxkbcommon-dev wayland-protocols libx11-dev libxrandr-dev libxinerama-dev libxi-dev libxcursor-dev libxext-dev
source /path/to/VulkanSDK/setup-env.sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r scripts/requirements-build.txt
meson setup build --buildtype=release -Dfile_dialogs=enabled
meson compile -C build
./build/vkrt
```

Meson downloads larger libraries through pinned wraps in `subprojects/`.
Smaller libraries are checked in under `external/`; their source files match the pinned upstream versions.
Project build rules live in `external/meson.build`, outside each vendor directory.

### Build options

| Option | Effect |
| --- | --- |
| `--buildtype=debug` | Enable Vulkan validation and debug logs. |
| `-Dprofiling=true` | Include debug symbols and profiling information. |
| `-Dlinux_window_backend=x11/wayland/both` | Select Linux window backends. Default: `both`. |
| `-Dnfd_backend=portal/gtk` | Select Linux file dialogs. Default: `portal`. GTK requires `libgtk-3-dev`. |
| `-Dfile_dialogs=disabled` | Build without native file dialogs. |

Use `meson configure build` to list all options.
After dependency or build-option changes, use a new build directory.

## Render an image

```sh
./build/vkrt --render-headless --scene assets/scenes/caustics.json --render-width 1920 --render-height 1080 --samples 4096 --denoise --output render.exr
```

On Windows, use `./build/vkrt.exe`.
The output extension selects EXR, PNG, or JPEG.
EXR preserves linear HDR values. PNG and JPEG use the exposure and tone mapping from the scene.
Use `--help` for more commands.

## Lint and format

Install the tools in the active Python environment:

```sh
python -m pip install -r scripts/requirements-lint.txt
```

After building, run C/C++ analysis and formatting from the repository root.
On PowerShell:

```powershell
run-clang-tidy -p build ([regex]::Escape("$PWD\src\"))
clang-format -i --assume-filename=source.cpp (git ls-files 'src/*.c' 'src/*.h' 'src/*.cpp' 'src/*.hpp' 'src/*.slang')
```

On Bash:

```sh
run-clang-tidy -p build "$PWD/src/"
git ls-files -z 'src/*.c' 'src/*.h' 'src/*.cpp' 'src/*.hpp' 'src/*.slang' | xargs -0 clang-format -i --assume-filename=source.cpp
```

For Python, on either platform:

```sh
ruff check scripts
ruff format scripts
```

Replace `-i` with `--dry-run --Werror` to check C/C++ and shader formatting without writing files.
Use `ruff format --check scripts` to check Python formatting.

## Packages and releases

Build first, then create a Windows ZIP or Linux tarball:

```sh
python scripts/package.py
```

Use `--build-dir <directory>` for another build directory. Packages are written to `dist/`.
Meson stages the executable, assets, and OIDN libraries. The packager adds runtime dependencies, licenses, and archives.

On Windows, use an x64 developer prompt to bundle the MSVC runtime from `VCToolsRedistDir`.
Without it, users need the Microsoft Visual C++ x64 Redistributable.
On Linux, install `patchelf` and `desktop-file-utils`. Add `--appimage` to also create an AppImage.

The [CI workflow](.github/workflows/ci.yml) builds and packages Windows and Linux. Linux also runs lint.
Push a `v*` tag to release both platforms after both jobs pass.
Release assets include the ZIP, tarball, AppImage, and `SHA256SUMS`. Tags with a hyphen create prereleases.

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

Exact versions, download links, and checksums are in:

- [Meson wraps](subprojects) for downloaded libraries.
- [Vendor manifest](external/dependencies.json) for bundled source, including stb and rgb2spec.
- [CI workflow](.github/workflows/ci.yml) for the Vulkan SDK versions and checksums.
- [AppImage tools](scripts/toolchain.json) for packaging tool versions and checksums.
- [Build tools](scripts/requirements-build.txt) and [analysis tools](scripts/requirements-lint.txt) for Python packages.

zlib and libjpeg-turbo use pinned WrapDB build files. OIDN uses a local wrapper for its upstream binaries.
These add build integration without changing upstream source.
The generated rgb2spec table is `src/core/scene/srgb.coeff`; its upstream license is in `external/rgb2spec/`.

Update bundled source from the versions and URLs in the vendor manifest. Keep its version and checksum current.
Keep vendor files unchanged. Set build options in the project build rules.
Update ImGui and Dear Bindings together, with matching ImGui tags.

Sample image from spectral mode:
<img width="1080" height="607" alt="image" src="https://github.com/user-attachments/assets/17a43e29-341f-4718-82ba-5b4ec32b923c" />
