#!/usr/bin/env python3

import argparse
import json
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


def resolve_file(entry):
    file_path = Path(entry["file"])
    if file_path.is_absolute():
        return file_path.resolve()

    return (Path(entry["directory"]) / file_path).resolve()


def main():
    args = parse_args()
    clang_tidy = shutil.which("clang-tidy")
    if clang_tidy is None:
        print("clang-tidy not found on PATH", file=sys.stderr)
        return 2

    compile_commands_path = BUILD_DIR / "compile_commands.json"
    if not compile_commands_path.is_file():
        print(f"Missing compile database: {compile_commands_path}", file=sys.stderr)
        return 2

    with compile_commands_path.open("r", encoding="utf-8") as handle:
        compile_commands = json.load(handle)

    selected = set()
    for raw_path in args.paths:
        path = (ROOT / raw_path).resolve()
        if path.is_dir():
            for child in path.rglob("*"):
                if child.is_file() and child.suffix.lower() in SOURCE_SUFFIXES:
                    selected.add(child.resolve())
        else:
            selected.add(path)

    files = []
    seen = set()
    for entry in compile_commands:
        file_path = resolve_file(entry)
        if file_path in seen:
            continue
        if file_path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        if not str(file_path).startswith(str(ROOT / "src")):
            continue
        if selected and file_path not in selected:
            continue

        seen.add(file_path)
        files.append(file_path.relative_to(ROOT))

    if not files:
        print("No matching source files found.", file=sys.stderr)
        return 1

    command = [clang_tidy, "-p", str(BUILD_DIR)]
    if args.fix:
        command.append("--fix")
    command.extend(str(path) for path in files)
    return subprocess.run(command, cwd=ROOT).returncode


if __name__ == "__main__":
    raise SystemExit(main())
