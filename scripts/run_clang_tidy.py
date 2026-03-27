#!/usr/bin/env python3

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
PYTHON_SUFFIXES = {".py", ".pyw"}
WINDOWS_EXECUTABLE_SUFFIXES = {".exe", ".bat", ".cmd", ".com"}


def parse_args():
    parser = argparse.ArgumentParser(description="Run clang-tidy for this repo.")
    parser.add_argument("--fix", action="store_true")
    parser.add_argument("paths", nargs="*")
    return parser.parse_args()


def iter_source_files(path):
    if path.is_file():
        if path.suffix.lower() in SOURCE_SUFFIXES:
            yield path.relative_to(ROOT)
        return
    if not path.is_dir():
        return
    for child in sorted(path.rglob("*")):
        if child.is_file() and child.suffix.lower() in SOURCE_SUFFIXES:
            yield child.relative_to(ROOT)


def collect_files(raw_paths):
    roots = (
        [(ROOT / raw_path).resolve() for raw_path in raw_paths]
        if raw_paths
        else [ROOT / "src"]
    )
    return list(
        dict.fromkeys(
            relative_path for root in roots for relative_path in iter_source_files(root)
        )
    )


def build_file_patterns(files):
    if not files:
        return [r"(^|.*[\\/])src[\\/].*\.(c|cc|cpp|cxx)$"]

    return [
        rf"(^|.*[\\/]){re.escape(path.as_posix()).replace('/', r'[\\/]')}$"
        for path in files
    ]


def is_python_script(path):
    suffix = path.suffix.lower()
    if suffix in PYTHON_SUFFIXES:
        return True
    if sys.platform != "win32" or suffix in WINDOWS_EXECUTABLE_SUFFIXES:
        return False
    if suffix:
        return False
    try:
        with path.open("r", encoding="utf-8") as handle:
            first_line = handle.readline(256)
    except OSError:
        return False
    return first_line.startswith("#!") and "python" in first_line.lower()


def resolve_command(command_name, env_name):
    command_path = os.environ.get(env_name) or shutil.which(command_name)
    if command_path is None:
        print(f"{command_name} not found on PATH", file=sys.stderr)
        return None

    path = Path(command_path)
    return [sys.executable, command_path] if is_python_script(path) else [command_path]


def main():
    args = parse_args()
    run_clang_tidy = resolve_command("run-clang-tidy", "RUN_CLANG_TIDY")
    if run_clang_tidy is None:
        return 2
    clang_tidy = resolve_command("clang-tidy", "CLANG_TIDY")
    if clang_tidy is None:
        return 2

    compile_commands_path = BUILD_DIR / "compile_commands.json"
    if not compile_commands_path.is_file():
        print(f"Missing compile database: {compile_commands_path}", file=sys.stderr)
        return 2

    files = collect_files(args.paths)
    file_patterns = build_file_patterns(files)
    command = [
        "-clang-tidy-binary",
        clang_tidy[-1],
        "-p",
        str(BUILD_DIR),
        "-j",
        str(max(1, (os.cpu_count() or 1) - 1)),
        "-quiet",
    ]
    command = run_clang_tidy + command
    if sys.platform == "darwin":
        sdk_path = subprocess.run(
            ["xcrun", "--show-sdk-path"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        if sdk_path:
            command.extend(["-extra-arg=-isysroot", f"-extra-arg={sdk_path}"])
    if args.fix:
        command.extend(["-fix", "-format"])
    command.extend(file_patterns)
    return subprocess.run(command, cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
