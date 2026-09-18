import json
import tarfile
import zipfile
from pathlib import Path

from download import download_verified

ROOT = Path(__file__).resolve().parents[1]


def archive_files(path):
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            return {name: archive.read(name) for name in archive.namelist() if not name.endswith("/")}
    with tarfile.open(path) as archive:
        return {
            member.name.split("/", 1)[1]: archive.extractfile(member).read()
            for member in archive.getmembers()
            if member.isfile() and "/" in member.name
        }


def main():
    dependencies = json.loads((ROOT / "external/dependencies.json").read_text())
    for name, dependency in dependencies.items():
        archive = download_verified(
            dependency["url"], ROOT / ".cache/vendor" / dependency["sha256"], dependency["sha256"]
        )
        contents = archive_files(archive)
        for source, destination in dependency["files"].items():
            path = ROOT / "external" / destination
            if not path.resolve().is_relative_to(ROOT / "external"):
                raise ValueError(f"Invalid vendor destination: {destination}")
            data = contents[source]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        print(f"{name}: {dependency['version']}")


if __name__ == "__main__":
    main()
