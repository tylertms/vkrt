# vkrt

Hardware path tracing in C with Vulkan, with Windows and Linux releases.

See the [build and release guide](docs/build.md) for prerequisites, platform commands, CI, and packaging.
The [dependency inventory](docs/dependencies.md) lists the pinned upstream versions.

```bash
git clone https://github.com/tylertms/vkrt
cd vkrt
python -m pip install -r scripts/requirements-build.txt
meson setup build --buildtype=release
meson compile -C build
./build/vkrt
```

Sample image from spectral mode:
<img width="1080" height="607" alt="image" src="https://github.com/user-attachments/assets/17a43e29-341f-4718-82ba-5b4ec32b923c" />
