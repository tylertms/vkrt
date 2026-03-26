#!/usr/bin/env python3

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}


def parse_args():
    parser = argparse.ArgumentParser(description="Run clang-tidy for this repo.")
    parser.add_argument("--fix", action="store_true")
    parser.add_argument("paths", nargs="*")
    return parser.parse_args()


def collect_files(raw_paths):
    files = []

    if raw_paths:
        for raw_path in raw_paths:
            path = (ROOT / raw_path).resolve()
            if path.is_dir():
                for child in sorted(path.rglob("*")):
                    if child.is_file() and child.suffix.lower() in SOURCE_SUFFIXES:
                        files.append(child.relative_to(ROOT))
            elif path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
                files.append(path.relative_to(ROOT))
    else:
        for child in sorted((ROOT / "src").rglob("*")):
            if child.is_file() and child.suffix.lower() in SOURCE_SUFFIXES:
                files.append(child.relative_to(ROOT))

    unique_files = []
    seen = set()
    for path in files:
        if path in seen:
            continue
        seen.add(path)
        unique_files.append(path)
    return unique_files


def build_file_patterns(files):
    if not files:
        return [r"src/.*\.(c|cc|cpp|cxx)$"]

    return [str(path) for path in files]


def main():
    args = parse_args()
    run_clang_tidy = os.environ.get("RUN_CLANG_TIDY") or shutil.which("run-clang-tidy")
    if run_clang_tidy is None:
        print("run-clang-tidy not found on PATH", file=sys.stderr)
        return 2
    clang_tidy = os.environ.get("CLANG_TIDY") or shutil.which("clang-tidy")
    if clang_tidy is None:
        print("clang-tidy not found on PATH", file=sys.stderr)
        return 2

    compile_commands_path = BUILD_DIR / "compile_commands.json"
    if not compile_commands_path.is_file():
        print(f"Missing compile database: {compile_commands_path}", file=sys.stderr)
        return 2

    files = collect_files(args.paths)
    file_patterns = build_file_patterns(files)
    command = [
        run_clang_tidy,
        "-clang-tidy-binary",
        clang_tidy,
        "-p",
        str(BUILD_DIR),
        "-j",
        str(max(1, (os.cpu_count() or 1) - 1)),
    ]
    if sys.platform == "darwin":
        sdk_path = subprocess.run(
            ["xcrun", "--show-sdk-path"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        if sdk_path:
            command.extend([f"-extra-arg=-isysroot", f"-extra-arg={sdk_path}"])
    if args.fix:
        command.extend(["-fix", "-format"])
    command.extend(file_patterns)
    return subprocess.run(command, cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
