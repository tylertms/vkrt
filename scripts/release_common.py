import configparser
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def build_metadata(build_dir, platform):
    metadata = json.loads((build_dir / "release.json").read_text(encoding="utf-8"))
    if metadata["platform"] != platform or metadata["cpu"] != "x86_64":
        raise ValueError(f"The release packager requires a {platform} x86-64 build.")
    return metadata


def runtime_files(metadata):
    root = Path(metadata["oidn_root"])
    directory = root / ("bin" if metadata["platform"] == "windows" else "lib")
    pattern = "*.dll" if metadata["platform"] == "windows" else "*.so*"
    files = sorted(path for path in directory.glob(pattern) if path.is_file())
    if not any("OpenImageDenoise_device_cpu" in path.name for path in files):
        raise RuntimeError(f"OIDN CPU device is missing from {directory}")
    return files


def prepare_output(path):
    path = path.absolute()
    if path.is_symlink():
        raise ValueError(f"Refusing to overwrite a symlink: {path}")
    resolved = path.resolve()
    if not resolved.is_relative_to(ROOT) or resolved == ROOT:
        raise ValueError(f"Release staging must stay inside {ROOT}: {resolved}")
    if resolved.exists():
        shutil.rmtree(resolved)
    resolved.mkdir(parents=True)
    return resolved


def copy_release_data(bundle_dir, metadata):
    shutil.copytree(ROOT / "assets", bundle_dir / "assets")
    for name in ("README.md", "LICENSE"):
        shutil.copy2(ROOT / name, bundle_dir / name)
    licenses = bundle_dir / "licenses"
    licenses.mkdir()
    for path in (ROOT / "external").rglob("*"):
        if path.is_file() and path.name.lower().startswith(("license", "notice", "copying")):
            target = licenses / path.relative_to(ROOT / "external")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    shutil.copy2(ROOT / "external/dependencies.json", licenses / "dependencies.json")
    shutil.copytree(
        Path(metadata["oidn_root"]) / "doc", licenses / "oidn", ignore=shutil.ignore_patterns("*.pdf")
    )
    for name in ("glfw", "zlib", "spng", "libjpeg-turbo"):
        wrap = configparser.ConfigParser(interpolation=None)
        wrap.read(ROOT / "subprojects" / f"{name}.wrap")
        source = ROOT / "subprojects" / wrap["wrap-file"]["directory"]
        for path in source.iterdir():
            if path.is_file() and path.name.lower().startswith(("license", "copying", "readme")):
                target = licenses / name / path.name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
