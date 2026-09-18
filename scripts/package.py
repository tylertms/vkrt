import argparse
import configparser
import hashlib
import json
import os
import platform
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def copy_licenses(bundle_dir):
    licenses = bundle_dir / "licenses"
    licenses.mkdir(exist_ok=True)
    for path in (ROOT / "external").rglob("*"):
        if path.is_file() and path.name.lower().startswith(("license", "notice", "copying")):
            target = licenses / path.relative_to(ROOT / "external")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    shutil.copy2(ROOT / "external/dependencies.json", licenses / "dependencies.json")
    for name in ("glfw", "zlib", "spng", "libjpeg-turbo"):
        wrap = configparser.ConfigParser(interpolation=None)
        wrap.read(ROOT / "subprojects" / f"{name}.wrap")
        source = ROOT / "subprojects" / wrap["wrap-file"]["directory"]
        for path in source.iterdir():
            if path.is_file() and path.name.lower().startswith(("license", "copying", "readme")):
                target = licenses / name / path.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)


def copy_msvc_runtime(redist_dir, bin_dir):
    directories = sorted(redist_dir.glob("x64/Microsoft.VC*.CRT"))
    if len(directories) != 1:
        raise ValueError(f"Expected one x64 CRT directory in {redist_dir}")
    libraries = sorted(directories[0].glob("*.dll"))
    required = {"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"}
    if not required.issubset({path.name.lower() for path in libraries}):
        raise ValueError(f"Incomplete MSVC runtime in {directories[0]}")
    for path in libraries:
        shutil.copy2(path, bin_dir / path.name)


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


def bundle_linux(bundle_dir):
    binary = bundle_dir / "bin/vkrt"
    library_dir = bundle_dir / "lib"
    required = [binary, *library_dir.glob("*OpenImageDenoise_device_cpu.so*")]
    dependencies = set()
    for path in required:
        dependencies.update(collect_ldd_paths(path, library_dir))
    for source in sorted(dependencies):
        if not should_bundle_library(source.name):
            continue
        resolved = source.resolve()
        destination = library_dir / resolved.name
        if not destination.exists():
            shutil.copy2(resolved, destination)
        if source.name != resolved.name and not (library_dir / source.name).exists():
            (library_dir / source.name).symlink_to(resolved.name)
        if source.is_relative_to("/usr") or source.is_relative_to("/lib"):
            copy_distribution_license(source, bundle_dir / "licenses/system")
    for library in sorted({path.resolve() for path in library_dir.iterdir()}):
        subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", str(library)], check=True)


def download_tool(name):
    settings = json.loads((ROOT / "scripts/toolchain.json").read_text())[name]
    path = ROOT / ".cache" / settings["sha256"]
    if not path.exists():
        subprocess.run(
            [
                "curl",
                "--fail",
                "--location",
                "--retry",
                "3",
                "--connect-timeout",
                "30",
                "--max-time",
                "900",
                "--output",
                str(path),
                settings["url"],
            ],
            check=True,
        )
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != settings["sha256"]:
        path.unlink()
        raise ValueError(f"Checksum mismatch for {name}; run the command again to download it.")
    path.chmod(0o755)
    return path


def build_appimage(bundle_dir, app_dir, output):
    tool = download_tool("appimagetool")
    runtime = download_tool("appimage_runtime")
    (app_dir / "usr").mkdir(parents=True)
    shutil.copytree(bundle_dir, app_dir / "usr", dirs_exist_ok=True, symlinks=True)
    (app_dir / "AppRun").symlink_to("usr/bin/vkrt")
    shutil.copy2(ROOT / "assets/images/icon.png", app_dir / "vkrt.png")
    (app_dir / "vkrt.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=vkrt\nExec=vkrt\nIcon=vkrt\nCategories=Graphics;\nTerminal=false\n",
        encoding="utf-8",
    )
    subprocess.run(
        [str(tool), "--appimage-extract-and-run", "--runtime-file", str(runtime), str(app_dir), str(output)],
        check=True,
        env={**os.environ, "ARCH": "x86_64"},
    )


def main():
    parser = argparse.ArgumentParser(description="Build and package a Windows ZIP or Linux tarball in dist/.")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--msvc-redist", type=Path, default=os.environ.get("VCToolsRedistDir"))
    parser.add_argument("--appimage", action="store_true", help="Also create a Linux AppImage.")
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    host = json.loads(subprocess.check_output(["meson", "introspect", str(build_dir), "--machines"]))["host"]
    system = platform.system().lower()
    if system not in ("windows", "linux") or host["system"] != system or host["cpu_family"] != "x86_64":
        parser.error("Packaging requires a native Windows or Linux x86-64 build.")
    if args.appimage and system != "linux":
        parser.error("--appimage requires Linux.")
    options = json.loads(subprocess.check_output(["meson", "introspect", str(build_dir), "--buildoptions"]))
    prefix = Path(next(option["value"] for option in options if option["name"] == "prefix"))
    for directory in (ROOT / ".cache", ROOT / "dist"):
        if not directory.resolve().is_relative_to(ROOT):
            parser.error(f"Package directories must stay inside {ROOT}: {directory}")
        directory.mkdir(exist_ok=True)
    output = ROOT / "dist" / f"vkrt-{system}-x64"
    with tempfile.TemporaryDirectory(prefix="vkrt-package-", dir=ROOT / ".cache") as temporary:
        install_root = Path(temporary) / "install"
        subprocess.run(
            ["meson", "install", "-C", str(build_dir), "--destdir", str(install_root), "--skip-subprojects"],
            check=True,
        )
        bundle_dir = install_root.joinpath(*prefix.parts[1:])
        runtime_dir = bundle_dir / ("bin" if system == "windows" else "lib")
        if not any(runtime_dir.glob("*OpenImageDenoise_device_cpu*")):
            raise RuntimeError("The installed package is missing the OIDN CPU fallback.")
        copy_licenses(bundle_dir)
        if system == "windows":
            if args.msvc_redist:
                copy_msvc_runtime(args.msvc_redist, runtime_dir)
            shutil.make_archive(str(output), "zip", root_dir=bundle_dir)
        else:
            bundle_linux(bundle_dir)
            shutil.make_archive(str(output), "gztar", root_dir=bundle_dir)
            if args.appimage:
                build_appimage(
                    bundle_dir, Path(temporary) / "AppDir", output.parent / "vkrt-linux-x86_64.AppImage"
                )


if __name__ == "__main__":
    main()
