import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from download import download_verified
from release_common import ROOT, build_metadata, copy_release_data, prepare_output, runtime_files


def collect_ldd_paths(binary_path, library_search_dir):
    env = {**os.environ, "LD_LIBRARY_PATH": str(library_search_dir)}
    result = subprocess.run(["ldd", str(binary_path)], check=True, capture_output=True, text=True, env=env)
    missing = []
    paths = set()
    for line in result.stdout.splitlines():
        fields = line.strip().split()
        if "not found" in line:
            missing.append(fields[0])
        elif "=>" in line:
            candidate = line.split("=>", 1)[1].strip().split()[0]
            if candidate.startswith("/"):
                paths.add(Path(candidate))
        elif fields and fields[0].startswith("/"):
            paths.add(Path(fields[0]))
    if missing:
        raise RuntimeError(f"Missing runtime libraries for {binary_path}: " + ", ".join(missing))
    return sorted(paths)


def should_bundle_library(name):
    return not name.startswith(
        (
            "linux-vdso.so.",
            "ld-linux",
            "libc.so.",
            "libm.so.",
            "libpthread.so.",
            "librt.so.",
            "libdl.so.",
            "libutil.so.",
            "libresolv.so.",
            "libnsl.so.",
            "libanl.so.",
            "libcrypt.so.",
            "libBrokenLocale.so.",
            "libvulkan.so.",
            "libGL.so.",
            "libGLX.so.",
            "libEGL.so.",
            "libOpenGL.so.",
            "libdrm.so.",
            "libgbm.so.",
            "libcuda.so.",
            "libnvidia-",
            "libamdhip64.so.",
            "libze_loader.so.",
        )
    )


def copy_runtime_libraries(binary_path, metadata, library_dir):
    oidn_files = runtime_files(metadata)
    dependencies = set(oidn_files)
    required = [binary_path] + [path for path in oidn_files if "OpenImageDenoise_device_cpu" in path.name]
    for path in required:
        dependencies.update(collect_ldd_paths(path, Path(metadata["oidn_root"]) / "lib"))
    for source in sorted(dependencies):
        if should_bundle_library(source.name):
            resolved = source.resolve()
            destination = library_dir / resolved.name
            if not destination.exists():
                shutil.copy2(resolved, destination)
            if source.name != resolved.name:
                link = library_dir / source.name
                if not link.exists():
                    link.symlink_to(resolved.name)
            if source.is_relative_to("/usr") or source.is_relative_to("/lib"):
                copy_distribution_license(source, library_dir.parent / "licenses/system")


def copy_distribution_license(source, license_dir):
    candidates = [source, source.resolve()]
    for candidate in candidates:
        result = subprocess.run(
            ["dpkg-query", "--search", str(candidate)], capture_output=True, text=True, check=False
        )
        if result.returncode == 0:
            package = result.stdout.splitlines()[0].rsplit(": ", 1)[0].split(":", 1)[0]
            copyright_file = Path("/usr/share/doc") / package / "copyright"
            if copyright_file.is_file():
                destination = license_dir / package
                destination.mkdir(parents=True, exist_ok=True)
                shutil.copy2(copyright_file, destination / "copyright")
                return
    raise RuntimeError(f"Could not locate the distribution copyright notice for {source}")


def set_bundle_rpaths(bundle_dir):
    subprocess.run(
        ["patchelf", "--set-rpath", "$ORIGIN/../lib", str(bundle_dir / "libexec/vkrt")], check=True
    )
    libraries = {path.resolve() for path in (bundle_dir / "lib").iterdir()}
    for library in sorted(libraries):
        subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(library)], check=True)


def write_launcher(path, executable, library_dir, root):
    path.write_text(
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        'HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"\n'
        f'export LD_LIBRARY_PATH="${{HERE}}/{library_dir}${{LD_LIBRARY_PATH:+:${{LD_LIBRARY_PATH}}}}"\n'
        f'cd "${{HERE}}/{root}"\n'
        f'exec "${{HERE}}/{executable}" "$@"\n',
        encoding="utf-8",
        newline="\n",
    )
    path.chmod(0o755)


def build_appimage(bundle_dir, output):
    toolchain = json.loads((ROOT / "scripts/toolchain.json").read_text())
    tool = toolchain["appimagetool"]
    tool_path = download_verified(tool["url"], ROOT / ".cache/appimage" / tool["sha256"], tool["sha256"])
    tool_path.chmod(0o755)
    runtime = toolchain["appimage_runtime"]
    runtime_path = download_verified(
        runtime["url"], ROOT / ".cache/appimage" / runtime["sha256"], runtime["sha256"]
    )
    with tempfile.TemporaryDirectory(prefix="vkrt-appimage-", dir=output.parent) as temporary:
        app_dir = Path(temporary) / "AppDir"
        shutil.copytree(bundle_dir, app_dir / "usr")
        shutil.copy2(ROOT / "assets/images/icon.png", app_dir / "vkrt.png")
        (app_dir / "vkrt.desktop").write_text(
            "[Desktop Entry]\nType=Application\nName=vkrt\nExec=vkrt\nIcon=vkrt\nCategories=Graphics;\nTerminal=false\n",
            encoding="utf-8",
        )
        write_launcher(app_dir / "AppRun", "usr/libexec/vkrt", "usr/lib", "usr")
        subprocess.run(
            [
                str(tool_path),
                "--appimage-extract-and-run",
                "--runtime-file",
                str(runtime_path),
                str(app_dir),
                str(output),
            ],
            check=True,
            env={**os.environ, "ARCH": "x86_64"},
        )


def main():
    parser = argparse.ArgumentParser(description="Package a Linux tarball and optional AppImage.")
    parser.add_argument("output_dir", nargs="?", type=Path, default=ROOT / "dist")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--appimage", action="store_true")
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    metadata = build_metadata(build_dir, "linux")
    binary_path = build_dir / "vkrt"
    if not binary_path.is_file():
        parser.error(f"Missing executable: {binary_path}")
    bundle_dir = prepare_output(args.output_dir / "vkrt-linux-x64")
    for directory in ("bin", "lib", "libexec"):
        (bundle_dir / directory).mkdir()
    shutil.copy2(binary_path, bundle_dir / "libexec/vkrt")
    copy_release_data(bundle_dir, metadata)
    copy_runtime_libraries(binary_path, metadata, bundle_dir / "lib")
    set_bundle_rpaths(bundle_dir)
    write_launcher(bundle_dir / "bin/vkrt", "../libexec/vkrt", "../lib", "..")
    shutil.make_archive(str(bundle_dir), "gztar", root_dir=bundle_dir)
    if args.appimage:
        build_appimage(bundle_dir, bundle_dir.parent / "vkrt-linux-x86_64.AppImage")


if __name__ == "__main__":
    main()
