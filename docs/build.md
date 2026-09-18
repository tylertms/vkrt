# Build and release

The supported release targets are Windows x86-64 and Linux x86-64.
Linux releases use Ubuntu 24.04 and require glibc 2.39 or newer.
Rendering requires a Vulkan driver and a GPU with ray tracing pipeline support.

## Build tools

Use Python 3.12 or newer. CI uses Python 3.14.
Install the pinned Meson, Ninja, and CMake versions:

```sh
python -m pip install -r scripts/requirements-build.txt
```

The compiler, operating system libraries, and Python patch version follow the selected runner image.
The build tools, downloaded libraries, SDK archives, and release tools have explicit version pins.
Every archive download requires a matching SHA256 checksum.

### Windows

Install Visual Studio Build Tools with the C++ desktop workload.
Open an x64 developer PowerShell prompt.
Install the project SDK:

```powershell
python scripts/setup_vulkan.py
$env:VULKAN_SDK = "$PWD/.cache/vulkan-sdk"
$env:PATH = "$env:VULKAN_SDK/Bin;$env:PATH"
meson setup build --buildtype=release -Db_vscrt=static_from_buildtype
meson compile -C build
./build/vkrt.exe
```

The SDK installer uses copy-only mode. It does not replace the system Vulkan runtime.
MinGW builds link their compiler runtime statically. CI uses MSVC.

### Linux

Install the development packages on Ubuntu 24.04:

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

Meson builds the pinned GLFW, zlib, libspng, and libjpeg-turbo sources.
It downloads the pinned OIDN binaries for the target platform.
It does not substitute older system versions of these libraries.
The Linux window and dialog backends use system development libraries.

## Build configuration

| Configuration | Purpose |
| --- | --- |
| `--buildtype=release` | Optimize the application and shaders. |
| `--buildtype=debug` | Enable tracing and Vulkan validation. |
| `-Dprofiling=true` | Enable profiling data and debug symbols. |
| `-Dlinux_window_backend=auto/x11/wayland/both` | Select the Linux window backends. |
| `-Dnfd_backend=auto/portal/gtk` | Select the Linux file dialog backend. GTK requires `libgtk-3-dev`. |
| `-Dfile_dialogs=auto/enabled/disabled` | Control native file dialogs. `enabled` fails if requirements are missing. |
| `-Dstatic_analysis=auto/enabled/disabled` | Control clang-tidy detection. Normal builds do not require it. |
| `-Dvkrt_version=VALUE` | Set the version in the executable. |

Build directories can have any name. Packaging and analysis accept `--build-dir`.
Meson tracks shader imports through Slang depfiles and embeds generated shaders in the executable.

## Checks

Install the analysis tools, then enable the Meson analysis target:

```sh
python -m pip install -r scripts/requirements-lint.txt
meson configure build -Dstatic_analysis=enabled
meson compile -C build
meson compile -C build clang-tidy
ruff check scripts
```

clang-tidy checks compiled project sources and project headers. It excludes dependency sources and generated files.
The CI rules cover analysis, bug patterns, CERT, performance, and portability.
Naming rules and other readability rules remain outside this correctness check.
Include cleanup is disabled because conditional platform includes produce conflicting results.
The rules accept `#pragma once` and preserve enum representations shared between C, C++, and shaders.

For selected files or a different build directory:

```sh
python scripts/run_clang_tidy.py --build-dir build-debug src/core
```

The `clang-tidy-fix` target applies fixes serially to avoid concurrent header edits.
CI runs analysis on Linux. Windows and Linux both compile shaders and link the application.

## Packages

Create a Windows ZIP from an x64 developer prompt:

```powershell
python scripts/package_windows.py dist --msvc-redist "$env:VCToolsRedistDir"
```

Official Windows packages include the MSVC runtime required by OIDN.
Local packages without `--msvc-redist` require the installed Microsoft Visual C++ x64 Redistributable.
The default value for this argument is `VCToolsRedistDir`, when available.

Create the Linux tarball and AppImage:

```sh
sudo apt-get install patchelf desktop-file-utils
python scripts/package_linux.py dist --appimage
```

Packaging includes assets, licenses, dependency notices, and OIDN device modules.
Linux packages include linked shared libraries and the OIDN runtime.
System C libraries, graphics drivers, and desktop services remain on the host.
Windowed operation requires system X11 or Wayland client libraries.
The executable and shared libraries use package-relative runtime paths.
AppImage creation uses pinned tooling and a pinned stable runtime. It does not require FUSE.

## CI and releases

`ci.yml` and `release.yml` call the same build workflow.
Both platforms compile and create release packages.
Linux also runs clang-tidy and Python checks.
Only tagged releases create an AppImage.

A pushed `v*` tag starts a release build.
The tag becomes the application version.
The publish job starts only after both platforms pass.
It uploads the Windows ZIP, Linux tarball, AppImage, and `SHA256SUMS` to a draft.
It publishes the draft only after all uploads succeed.
Tags with a hyphen create prereleases.

Build jobs have read-only repository permissions. Only the publish job can write release assets.
CI cancels superseded runs. Release runs finish without cancellation.
Caches contain downloaded dependencies and the SDK, not compiled project files.

## Dependency updates

The dependency inventory is in [dependencies.md](dependencies.md).
The vendor manifest records upstream versions, archive checksums, and file mappings.
To restore those exact files:

```sh
python scripts/sync_vendor.py
```

For a library update, change its wrap or vendor manifest entry to a verified upstream release.
Update ImGui and Dear Bindings together, using the same ImGui docking tag.
Then restore the vendor files and repeat the build, analysis, and packaging steps.
The rgb2spec coefficient table is generated application data. Vendor restoration preserves it.

For SDK and AppImage updates, change `scripts/toolchain.json` with the upstream archive checksum.
Use a fresh SDK directory with `setup_vulkan.py --directory` after a version change.
Use a fresh build directory after dependency changes.
If a wrap overlay changed, refresh that subproject before configuration:

```sh
meson subprojects update --reset oidn_windows_x86_64
```

Replace the OIDN subproject name with the target platform name when necessary.
GitHub Actions use commit pins. Update each pin against its upstream release before changing the workflow.
