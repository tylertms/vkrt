#!/usr/bin/env python3

import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Stage a release bundle for this repo."
    )
    parser.add_argument("platform", choices=["windows", "linux"])
    parser.add_argument("output_dir", nargs="?", default=".")
    parser.add_argument("--zip", action="store_true")
    return parser.parse_args()


def remove_path(path):
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def copy_runtime_files(bundle_dir, patterns):
    copied = set()
    for pattern in patterns:
        for source in BUILD_DIR.glob(pattern):
            if not source.is_file() or source in copied:
                continue
            shutil.copy2(source, bundle_dir / source.name)
            copied.add(source)


def main():
    args = parse_args()
    output_dir = (ROOT / args.output_dir).resolve()
    archive_name = (
        "vkrt-windows-x64" if args.platform == "windows" else "vkrt-linux-x64"
    )
    binary_name = "vkrt.exe" if args.platform == "windows" else "vkrt"
    runtime_patterns = (
        ["OpenImageDenoise*.dll", "tbb*.dll"]
        if args.platform == "windows"
        else ["libOpenImageDenoise*.so*", "libtbb*.so*"]
    )

    binary_path = BUILD_DIR / binary_name
    bundle_dir = output_dir / archive_name
    archive_path = output_dir / f"{archive_name}.zip"

    if not binary_path.is_file():
        raise RuntimeError(f"missing executable: {binary_path}")

    if bundle_dir.exists() or bundle_dir.is_symlink():
        remove_path(bundle_dir)
    if args.zip and (archive_path.exists() or archive_path.is_symlink()):
        remove_path(archive_path)

    bundle_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary_path, bundle_dir / binary_name)
    copy_runtime_files(bundle_dir, runtime_patterns)
    shutil.copytree(ROOT / "assets", bundle_dir / "assets", symlinks=True)
    shutil.copy2(ROOT / "README.md", bundle_dir / "README.md")

    if args.zip:
        shutil.make_archive(
            str(archive_path.with_suffix("")), "zip", bundle_dir.parent, bundle_dir.name
        )

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(exc, file=sys.stderr)
        raise SystemExit(1)
