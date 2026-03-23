#!/usr/bin/env python3
from __future__ import annotations

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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--appimage", action="store_true")
    return parser.parse_args()


def env_path(name: str, default: str) -> Path:
    return Path(os.environ.get(name, default))


def remove_path(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def ensure_clean(paths: list[Path]) -> None:
    for path in paths:
        if path.exists() or path.is_symlink():
            remove_path(path)


def copy_tree(src: Path, dst: Path) -> None:
    shutil.copytree(src, dst, symlinks=True)


def copy_file(src: Path, dst: Path, mode: int | None = None) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    if mode is not None:
        dst.chmod(mode)


def write_text(path: Path, content: str, mode: int | None = None) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    if mode is not None:
        path.chmod(mode)


def collect_ldd_paths(binary_path: Path) -> list[Path]:
    result = subprocess.run(
        ["ldd", str(binary_path)],
        check=True,
        capture_output=True,
        text=True,
    )

    missing = []
    library_paths = []

    for line in result.stdout.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if "not found" in stripped:
            missing.append(stripped.split()[0])
            continue
        if "=>" in stripped:
            fields = stripped.split("=>", 1)[1].strip().split()
            if fields and fields[0].startswith("/"):
                library_paths.append(Path(fields[0]))
            continue
        if stripped.startswith("/"):
            library_paths.append(Path(stripped.split()[0]))

    if missing:
        raise RuntimeError("missing runtime libraries:\n" + "\n".join(missing))

    seen: set[Path] = set()
    ordered_paths: list[Path] = []
    for path in library_paths:
        if path not in seen:
            seen.add(path)
            ordered_paths.append(path)
    return ordered_paths


def should_bundle_library(name: str) -> bool:
    excluded_prefixes = (
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
    return not name.startswith(excluded_prefixes)


def stage_bundle(binary_path: Path, app_dir: Path, repo_root: Path) -> None:
    app_bin_dir = app_dir / "usr" / "bin"
    app_lib_dir = app_dir / "usr" / "lib"
    app_icon_path = app_dir / "vkrt.png"
    app_bin_dir.mkdir(parents=True, exist_ok=True)
    app_lib_dir.mkdir(parents=True, exist_ok=True)

    copy_file(binary_path, app_bin_dir / "vkrt", mode=0o755)
    copy_tree(repo_root / "assets", app_bin_dir / "assets")
    copy_file(repo_root / "README.md", app_bin_dir / "README.md", mode=0o644)
    copy_file(repo_root / "assets" / "images" / "icon.png", app_icon_path, mode=0o644)

    write_text(
        app_dir / "vkrt.desktop",
        "\n".join(
            [
                "[Desktop Entry]",
                "Type=Application",
                "Name=vkrt",
                "Exec=vkrt",
                "Icon=vkrt",
                "Categories=Graphics;",
                "Terminal=false",
                "",
            ]
        ),
        mode=0o644,
    )

    write_text(
        app_dir / "AppRun",
        "\n".join(
            [
                "#!/usr/bin/env bash",
                "set -euo pipefail",
                'HERE="$(dirname "$(readlink -f "$0")")"',
                'export LD_LIBRARY_PATH="${HERE}/usr/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"',
                'exec "${HERE}/usr/bin/vkrt" "$@"',
                "",
            ]
        ),
        mode=0o755,
    )

    for library_path in collect_ldd_paths(app_bin_dir / "vkrt"):
        library_name = library_path.name
        if not should_bundle_library(library_name):
            continue

        target_path = app_lib_dir / library_name
        if target_path.exists():
            continue

        resolved_path = library_path.resolve()
        copy_file(resolved_path, target_path, mode=resolved_path.stat().st_mode & 0o777)


def write_tarball(
    app_dir: Path, bundle_dir: Path, tarball_path: Path, repo_root: Path
) -> None:
    bundle_dir.mkdir(parents=True, exist_ok=True)
    copy_file(app_dir / "AppRun", bundle_dir / "vkrt", mode=0o755)
    copy_file(repo_root / "README.md", bundle_dir / "README.md", mode=0o644)
    copy_tree(app_dir / "usr", bundle_dir / "usr")

    tarball_path.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(tarball_path, "w:gz") as archive:
        archive.add(bundle_dir, arcname=bundle_dir.name)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_appimage(app_dir: Path, appimage_path: Path, appimagetool_path: Path) -> None:
    subprocess.run(
        [str(appimagetool_path), str(app_dir), str(appimage_path)],
        check=True,
        env={**os.environ, "ARCH": "x86_64"},
    )


def download_appimagetool(repo_root: Path) -> Path:
    version = os.environ.get("APPIMAGETOOL_VERSION", "")
    expected_sha256 = os.environ.get("APPIMAGETOOL_SHA256", "")
    if not version or not expected_sha256:
        raise RuntimeError(
            "APPIMAGETOOL_VERSION and APPIMAGETOOL_SHA256 are required for --appimage"
        )

    destination = repo_root / "appimagetool"
    url = f"https://github.com/AppImage/appimagetool/releases/download/{version}/appimagetool-x86_64.AppImage"
    urllib.request.urlretrieve(url, destination)

    actual_sha256 = sha256_file(destination)
    if actual_sha256.lower() != expected_sha256.lower():
        raise RuntimeError(
            f"AppImageTool hash mismatch: expected {expected_sha256}, got {actual_sha256}"
        )

    destination.chmod(
        destination.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH
    )
    return destination


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent

    build_dir = env_path("BUILD_DIR", "build")
    app_dir = env_path("APP_DIR", "AppDir")
    bundle_dir = env_path("BUNDLE_DIR", "vkrt-linux-x64")
    tarball_path = env_path("TARBALL_PATH", "vkrt-linux-x64.tar.gz")
    appimage_path = env_path("APPIMAGE_PATH", "vkrt-linux-x86_64.AppImage")
    appimagetool_path = repo_root / "appimagetool"
    binary_path = build_dir / "vkrt"

    if not binary_path.is_file():
        raise RuntimeError(f"missing executable: {binary_path}")

    ensure_clean([app_dir, bundle_dir, tarball_path, appimage_path, appimagetool_path])

    stage_bundle(binary_path, app_dir, repo_root)
    write_tarball(app_dir, bundle_dir, tarball_path, repo_root)

    if args.appimage:
        downloaded_tool = download_appimagetool(repo_root)
        write_appimage(app_dir, appimage_path, downloaded_tool)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
