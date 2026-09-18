import argparse
import os
import shutil
from pathlib import Path

from release_common import ROOT, build_metadata, copy_release_data, prepare_output, runtime_files


def copy_msvc_runtime(redist_dir, bin_dir):
    directories = sorted(redist_dir.glob("x64/Microsoft.VC*.CRT"))
    if len(directories) != 1:
        raise ValueError(f"Expected one x64 CRT directory in {redist_dir}")
    libraries = sorted(directories[0].glob("*.dll"))
    required = {"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"}
    if not required.issubset({path.name.lower() for path in libraries}):
        raise ValueError(f"Incomplete MSVC runtime in {directories[0]}")
    for path in libraries:
        shutil.copy2(path, bin_dir / path.name)


def main():
    parser = argparse.ArgumentParser(description="Package a Windows release ZIP.")
    parser.add_argument("output_dir", nargs="?", type=Path, default=ROOT / "dist")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--msvc-redist", type=Path, default=os.environ.get("VCToolsRedistDir"))
    args = parser.parse_args()
    build_dir = args.build_dir.resolve()
    metadata = build_metadata(build_dir, "windows")
    executable = build_dir / "vkrt.exe"
    if not executable.is_file():
        parser.error(f"Missing executable: {executable}")
    bundle_dir = prepare_output(args.output_dir / "vkrt-windows-x64")
    bin_dir = bundle_dir / "bin"
    bin_dir.mkdir()
    shutil.copy2(executable, bin_dir / executable.name)
    for source in runtime_files(metadata):
        shutil.copy2(source, bin_dir / source.name)
    if args.msvc_redist:
        copy_msvc_runtime(args.msvc_redist, bin_dir)
    copy_release_data(bundle_dir, metadata)
    shutil.make_archive(str(bundle_dir), "zip", root_dir=bundle_dir)


if __name__ == "__main__":
    main()
