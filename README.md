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

Install Git and Python 3.12 or newer.
Get the source and install the build tools:

```sh
git clone https://github.com/tylertms/vkrt
cd vkrt
python -m pip install -r scripts/requirements-build.txt
```

### Windows

Install Visual Studio Build Tools with the C++ desktop workload.
Open an x64 developer PowerShell prompt in the repository directory:

```powershell
python scripts/setup_vulkan.py
$env:VULKAN_SDK = "$PWD/.cache/vulkan-sdk"
$env:PATH = "$env:VULKAN_SDK/Bin;$env:PATH"
meson setup build --buildtype=release -Db_vscrt=static_from_buildtype
meson compile -C build
./build/vkrt.exe
```

### Linux

On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install build-essential pkg-config nasm libvulkan1 libdbus-1-dev libwayland-dev libxkbcommon-dev wayland-protocols libx11-dev libxrandr-dev libxinerama-dev libxi-dev libxcursor-dev libxext-dev
python scripts/setup_vulkan.py
export VULKAN_SDK="$PWD/.cache/vulkan-sdk"
export PATH="$VULKAN_SDK/bin:$PATH"
meson setup build --buildtype=release -Dlinux_window_backend=both -Dnfd_backend=portal -Dfile_dialogs=enabled
meson compile -C build
./build/vkrt
```

The SDK script downloads the required Vulkan SDK and Slang compiler into `.cache/vulkan-sdk`.
Meson downloads and builds the remaining external libraries.

### Build options

| Option | Effect |
| --- | --- |
| `--buildtype=debug` | Enable Vulkan validation and debug logs. |
| `-Dprofiling=true` | Include debug symbols and profiling information. |
| `-Dlinux_window_backend=x11/wayland/both` | Select the Linux window backends. |
| `-Dnfd_backend=portal/gtk` | Select Linux file dialogs. GTK requires `libgtk-3-dev`. |
| `-Dfile_dialogs=disabled` | Build without native file dialogs. |

Use `meson configure build` to list all options.
After a dependency update, use a new build directory.

## Render an image

```sh
./build/vkrt --render-headless --scene assets/scenes/caustics.json --render-width 1920 --render-height 1080 --samples 4096 --denoise --output render.exr
```

On Windows, use `./build/vkrt.exe`.
The output extension selects EXR, PNG, or JPEG.
EXR preserves linear HDR values. PNG and JPEG use the exposure and tone mapping from the scene.
Use `--help` for more commands.

## Static analysis

```sh
python -m pip install -r scripts/requirements-lint.txt
meson configure build -Dstatic_analysis=enabled
meson compile -C build clang-tidy
ruff check scripts
```

## Packages and releases

On Windows, use an x64 developer PowerShell prompt:

```powershell
python scripts/package_windows.py dist --msvc-redist "$env:VCToolsRedistDir"
```

This command creates a ZIP with the required MSVC runtime.
Without `--msvc-redist` or `VCToolsRedistDir`, users need the Microsoft Visual C++ x64 Redistributable.

On Linux:

```sh
sudo apt-get install patchelf desktop-file-utils
python scripts/package_linux.py dist --appimage
```

This command creates a tarball and an AppImage.
Both packaging scripts accept `--build-dir` and write their output to `dist/`.

CI builds and packages both platforms. Linux also runs static analysis.
Push a `v*` tag to publish a release after both builds pass.
Release files include the Windows ZIP, Linux tarball, AppImage, and `SHA256SUMS`.
Tags with a hyphen create prereleases.

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
- [Toolchain manifest](scripts/toolchain.json) for the Vulkan SDK and AppImage tools.
- [Build tools](scripts/requirements-build.txt) and [analysis tools](scripts/requirements-lint.txt) for Python packages.

To restore bundled source from the vendor manifest:

```sh
python scripts/sync_vendor.py
```

Update ImGui and Dear Bindings together, with matching ImGui tags.

Sample image from spectral mode:
<img width="1080" height="607" alt="image" src="https://github.com/user-attachments/assets/17a43e29-341f-4718-82ba-5b4ec32b923c" />
