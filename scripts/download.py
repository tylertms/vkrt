import hashlib
import subprocess
from pathlib import Path


def sha256_file(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def download_verified(url, destination, expected_sha256):
    destination = Path(destination)
    if destination.is_file() and sha256_file(destination) == expected_sha256:
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".part")
    try:
        subprocess.run(
            [
                "curl",
                "--fail",
                "--location",
                "--retry",
                "3",
                "--connect-timeout",
                "30",
                "--max-time",
                "900",
                "--output",
                str(temporary),
                url,
            ],
            check=True,
        )
        actual = sha256_file(temporary)
        if actual != expected_sha256:
            raise RuntimeError(f"SHA256 mismatch for {url}: expected {expected_sha256}, got {actual}")
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)
    return destination
