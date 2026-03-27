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
    extensions = {".c", ".h", ".cpp", ".hpp"}
    files = sorted(path for path in src_root.rglob("*") if path.suffix in extensions)

    if not files:
        return 0

    clang_format = find_clang_format()
    command = [clang_format, "-i", *[str(path) for path in files]]
    completed = subprocess.run(command, cwd=repo_root)
    return completed.returncode


if __name__ == "__main__":
    sys.exit(main())
