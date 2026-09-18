import argparse
import json
import os
import platform
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

from download import download_verified

ROOT = Path(__file__).resolve().parents[1]


def setup_sdk(destination, system):
    settings = json.loads((ROOT / "scripts/toolchain.json").read_text())["vulkan"][system]
    marker = destination / ".vkrt-sdk.json"
    binary_dir = destination / ("Bin" if system == "windows" else "bin")
    compiler = binary_dir / ("slangc.exe" if system == "windows" else "slangc")
    if not (marker.is_file() and json.loads(marker.read_text()) == settings and compiler.is_file()):
        suffix = ".exe" if system == "windows" else ".tar.xz"
        archive = download_verified(
            settings["url"], ROOT / ".cache/downloads" / (settings["sha256"] + suffix), settings["sha256"]
        )
        if destination.exists():
            raise RuntimeError(f"Incomplete or different SDK at {destination}. Use an empty SDK directory.")
        destination.parent.mkdir(parents=True, exist_ok=True)
        if system == "windows":
            subprocess.run(
                [
                    str(archive),
                    "--root",
                    str(destination),
                    "--accept-licenses",
                    "--default-answer",
                    "--confirm-command",
                    "install",
                    "copy_only=1",
                ],
                check=True,
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
        else:
            with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
                with tarfile.open(archive) as bundle:
                    bundle.extractall(temporary, filter="data")
                source = Path(temporary) / settings["version"] / "x86_64"
                shutil.move(str(source), str(destination))
        if not compiler.is_file():
            raise RuntimeError(f"SDK installation did not provide {compiler}")
        marker.write_text(json.dumps(settings), encoding="utf-8")
    subprocess.run([str(compiler), "-version"], check=True)
    return binary_dir


def main():
    parser = argparse.ArgumentParser(
        description="Install the pinned Vulkan SDK without changing the system SDK."
    )
    parser.add_argument("--directory", type=Path, default=ROOT / ".cache/vulkan-sdk")
    args = parser.parse_args()
    system = platform.system().lower()
    if system not in ("windows", "linux") or platform.machine().lower() not in ("amd64", "x86_64"):
        parser.error("The CI toolchain supports Windows and Linux x86-64.")
    destination = args.directory.resolve()
    binary_dir = setup_sdk(destination, system)
    if "GITHUB_ENV" in os.environ:
        with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as stream:
            stream.write(f"VULKAN_SDK={destination}\n")
        with Path(os.environ["GITHUB_PATH"]).open("a", encoding="utf-8") as stream:
            stream.write(f"{binary_dir}\n")
    print(f"VULKAN_SDK={destination}")
    print(f"Add to PATH: {binary_dir}")


if __name__ == "__main__":
    main()
