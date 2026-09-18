import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def source_files(build_dir, paths):
    database = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    roots = [(ROOT / path).resolve() for path in paths] or [ROOT / "src"]
    for root in roots:
        if not root.exists() or not root.is_relative_to(ROOT / "src"):
            raise ValueError(f"Expected a source file or directory under src: {root}")
    files = {(Path(entry["directory"]) / entry["file"]).resolve() for entry in database}
    selected = sorted(
        path for path in files if any(path == root or path.is_relative_to(root) for root in roots)
    )
    if not selected:
        raise ValueError("No compiled project sources match the requested paths.")
    return selected


def main():
    parser = argparse.ArgumentParser(
        description="Run clang-tidy on project sources in the compilation database."
    )
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--fix", action="store_true")
    parser.add_argument("paths", nargs="*")
    args = parser.parse_args()
    clang_tidy = os.environ.get("CLANG_TIDY") or shutil.which("clang-tidy")
    if not clang_tidy:
        parser.error("clang-tidy is not available. Install scripts/requirements-lint.txt.")
    if args.jobs < 1:
        parser.error("--jobs must be positive.")
    build_dir = args.build_dir.resolve()
    try:
        files = source_files(build_dir, args.paths)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    command = [clang_tidy, "-p", str(build_dir), "--quiet"]
    if args.fix:
        command += ["--fix", "--format-style=file"]

    def analyze(path):
        result = subprocess.run(command + [str(path)], cwd=ROOT, capture_output=True, text=True, check=False)
        return path, result

    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=1 if args.fix else args.jobs) as pool:
        for path, result in pool.map(analyze, files):
            print(path.relative_to(ROOT), flush=True)
            print(result.stdout + result.stderr, end="", flush=True)
            failed += result.returncode != 0
    print(f"Analyzed {len(files)} files; {failed} failed.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
