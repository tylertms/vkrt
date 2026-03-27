#!/usr/bin/env python3

import shutil
import subprocess
import sys
from pathlib import Path


def find_clang_format() -> str:
    for candidate in (
        "clang-format",
        "clang-format.exe",
        "/opt/homebrew/opt/llvm/bin/clang-format",
    ):
        path = shutil.which(candidate) if "/" not in candidate else candidate
        if path and Path(path).exists():
            return path
    raise SystemExit("clang-format not found")


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    src_root = repo_root / "src"
    source_extensions = {".c", ".h", ".cpp", ".hpp"}
    shader_extensions = {".slang"}
    source_files = sorted(
        path for path in src_root.rglob("*") if path.suffix in source_extensions
    )
    shader_files = sorted(
        path for path in src_root.rglob("*") if path.suffix in shader_extensions
    )

    if not source_files and not shader_files:
        return 0

    clang_format = find_clang_format()

    if source_files:
        command = [clang_format, "-i", *[str(path) for path in source_files]]
        completed = subprocess.run(command, cwd=repo_root)
        if completed.returncode != 0:
            return completed.returncode

    for path in shader_files:
        command = [
            clang_format,
            "-i",
            f"--assume-filename={path.with_suffix('.cpp')}",
            str(path),
        ]
        completed = subprocess.run(command, cwd=repo_root)
        if completed.returncode != 0:
            return completed.returncode

    return 0


if __name__ == "__main__":
    sys.exit(main())
