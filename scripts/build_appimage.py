#!/usr/bin/env python3

import argparse
import hashlib
import os
import shutil
import stat
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build"
APP_DIR = ROOT / "AppDir"


def parse_args():
    parser = argparse.ArgumentParser(description="Build the Linux release bundle.")
    parser.add_argument("output_dir", nargs="?", default=".")
    parser.add_argument("--appimage", action="store_true")
    return parser.parse_args()


def remove_path(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def chmod_executable(path):
    path.chmod(0o755)


def collect_ldd_paths(binary_path, library_search_dir):
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = ":".join(
        part
        for part in [str(library_search_dir), env.get("LD_LIBRARY_PATH", "")]
        if part
    )

    result = subprocess.run(
        ["ldd", str(binary_path)],
        check=True,
        capture_output=True,
        text=True,
        env=env,
    )

    missing = []
    library_paths = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        if "not found" in line:
            missing.append(line.split()[0])
            continue
        if "=>" in line:
            fields = line.split("=>", 1)[1].strip().split()
            if fields and fields[0].startswith("/"):
                library_paths.append(Path(fields[0]))
            continue
        if line.startswith("/"):
            library_paths.append(Path(line.split()[0]))

    if missing:
        raise RuntimeError("missing runtime libraries:\n" + "\n".join(missing))

    unique_paths = []
    seen = set()
    for path in library_paths:
        if path in seen:
            continue
        seen.add(path)
        unique_paths.append(path)
    return unique_paths


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
        )
    )


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download_appimagetool(path):
    version = os.environ.get("APPIMAGETOOL_VERSION")
    expected_sha256 = os.environ.get("APPIMAGETOOL_SHA256")
    if not version or not expected_sha256:
        raise RuntimeError(
            "APPIMAGETOOL_VERSION and APPIMAGETOOL_SHA256 are required for --appimage"
        )

    if path.is_file():
        actual_sha256 = sha256_file(path)
        if actual_sha256.lower() == expected_sha256.lower():
            path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
            return
        path.unlink()

    urllib.request.urlretrieve(
        f"https://github.com/AppImage/appimagetool/releases/download/{version}/appimagetool-x86_64.AppImage",
        path,
    )

    actual_sha256 = sha256_file(path)
    if actual_sha256.lower() != expected_sha256.lower():
        raise RuntimeError(
            f"AppImageTool hash mismatch: expected {expected_sha256}, got {actual_sha256}"
        )

    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def main():
    args = parse_args()
    output_dir = (ROOT / args.output_dir).resolve()
    bundle_dir = output_dir / "vkrt-linux-x64"
    tarball_path = output_dir / "vkrt-linux-x64.tar.gz"
    appimage_path = output_dir / "vkrt-linux-x86_64.AppImage"
    appimagetool_path = ROOT / "appimagetool"
    binary_path = BUILD_DIR / "vkrt"

    if not binary_path.is_file():
        raise RuntimeError(f"missing executable: {binary_path}")

    for path in [APP_DIR, bundle_dir, tarball_path, appimage_path]:
        if path.exists() or path.is_symlink():
            remove_path(path)

    bundle_bin_dir = bundle_dir / "bin"
    bundle_lib_dir = bundle_dir / "lib"
    bundle_libexec_dir = bundle_dir / "libexec"
    app_lib_dir = APP_DIR / "usr" / "lib"
    app_libexec_dir = APP_DIR / "usr" / "libexec"

    for path in [
        bundle_bin_dir,
        bundle_lib_dir,
        bundle_libexec_dir,
        app_lib_dir,
        app_libexec_dir,
    ]:
        path.mkdir(parents=True, exist_ok=True)

    for target_path in [bundle_libexec_dir / "vkrt", app_libexec_dir / "vkrt"]:
        shutil.copy2(binary_path, target_path)
        chmod_executable(target_path)

    launcher = (
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        'HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"\n'
        'ROOT="$(cd -- "${HERE}/.." && pwd)"\n'
        'export LD_LIBRARY_PATH="${ROOT}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"\n'
        'cd "${ROOT}"\n'
        'exec "${ROOT}/libexec/vkrt" "$@"\n'
    )
    (bundle_bin_dir / "vkrt").write_text(launcher, encoding="utf-8")
    chmod_executable(bundle_bin_dir / "vkrt")

    for target_root in [bundle_dir, APP_DIR / "usr"]:
        shutil.copytree(ROOT / "assets", target_root / "assets", symlinks=True)
        shutil.copy2(ROOT / "README.md", target_root / "README.md")
    shutil.copy2(ROOT / "assets" / "images" / "icon.png", APP_DIR / "vkrt.png")

    (APP_DIR / "vkrt.desktop").write_text(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=vkrt\n"
        "Exec=vkrt\n"
        "Icon=vkrt\n"
        "Categories=Graphics;\n"
        "Terminal=false\n",
        encoding="utf-8",
    )
    (APP_DIR / "AppRun").write_text(
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        'HERE="$(dirname "$(readlink -f "$0")")"\n'
        'export LD_LIBRARY_PATH="${HERE}/usr/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"\n'
        'cd "${HERE}/usr"\n'
        'exec "${HERE}/usr/libexec/vkrt" "$@"\n',
        encoding="utf-8",
    )
    chmod_executable(APP_DIR / "AppRun")

    for library_path in collect_ldd_paths(binary_path, BUILD_DIR):
        if not should_bundle_library(library_path.name):
            continue
        resolved_path = library_path.resolve()
        for target_path in [
            bundle_lib_dir / library_path.name,
            app_lib_dir / library_path.name,
        ]:
            if target_path.exists():
                continue
            shutil.copy2(resolved_path, target_path)
            target_path.chmod(resolved_path.stat().st_mode & 0o777)

    bundle_dir.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(tarball_path, "w:gz") as archive:
        for path in sorted(bundle_dir.iterdir()):
            archive.add(path, arcname=path.name)

    if args.appimage:
        download_appimagetool(appimagetool_path)
        subprocess.run(
            [str(appimagetool_path), str(APP_DIR), str(appimage_path)],
            check=True,
            env={**os.environ, "ARCH": "x86_64"},
        )

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
