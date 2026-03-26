#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", choices=["windows", "linux"], required=True)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--bundle-dir", type=Path, required=True)
    parser.add_argument("--archive", choices=["none", "zip"], default="none")
    parser.add_argument("--archive-path", type=Path)
    return parser.parse_args()


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def remove_path(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def ensure_clean(path: Path) -> None:
    if path.exists() or path.is_symlink():
        remove_path(path)


def copy_runtime_files(build_dir: Path, bundle_dir: Path, patterns: list[str]) -> None:
    copied: set[Path] = set()
    for pattern in patterns:
        for source in build_dir.glob(pattern):
            if not source.is_file() or source in copied:
                continue
            shutil.copy2(source, bundle_dir / source.name)
            copied.add(source)


def stage_bundle(
    platform_name: str, build_dir: Path, bundle_dir: Path, root: Path
) -> None:
    binary_name = "vkrt.exe" if platform_name == "windows" else "vkrt"
    runtime_patterns = (
        ["OpenImageDenoise*.dll", "tbb*.dll"]
        if platform_name == "windows"
        else [
            "libOpenImageDenoise*.so*",
            "libtbb*.so*",
        ]
    )

    binary_path = build_dir / binary_name
    if not binary_path.is_file():
        raise RuntimeError(f"missing executable: {binary_path}")

    ensure_clean(bundle_dir)
    bundle_dir.mkdir(parents=True, exist_ok=True)

    shutil.copy2(binary_path, bundle_dir / binary_name)
    copy_runtime_files(build_dir, bundle_dir, runtime_patterns)
    shutil.copytree(root / "assets", bundle_dir / "assets", symlinks=True)
    shutil.copy2(root / "README.md", bundle_dir / "README.md")


def write_zip_archive(bundle_dir: Path, archive_path: Path) -> None:
    ensure_clean(archive_path)
    archive_path.parent.mkdir(parents=True, exist_ok=True)
    archive_base = archive_path.with_suffix("")
    shutil.make_archive(str(archive_base), "zip", bundle_dir.parent, bundle_dir.name)


def main() -> int:
    args = parse_args()
    root = repo_root()
    build_dir = (root / args.build_dir).resolve()
    bundle_dir = (root / args.bundle_dir).resolve()

    stage_bundle(args.platform, build_dir, bundle_dir, root)

    if args.archive == "zip":
        archive_path = args.archive_path
        if archive_path is None:
            archive_path = args.bundle_dir.with_suffix(".zip")
        archive_path = (root / archive_path).resolve()
        write_zip_archive(bundle_dir, archive_path)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
