#!/usr/bin/env python3
"""Download the pinned official validation tool into a build directory."""
import argparse
import hashlib
from pathlib import Path
import platform
import urllib.request
import zipfile

ASSETS = {
    "Linux": ("Linux", "c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352", "pluginval"),
    "Darwin": ("macOS", "3c4c533bda0c5059eea3ddaea752d757ee2025041f0f47e6bcb0e87f6082b29f", "pluginval.app/Contents/MacOS/pluginval"),
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    name, checksum, binary = ASSETS[platform.system()]
    args.directory.mkdir(parents=True, exist_ok=True)
    archive = args.directory / f"pluginval_{name}.zip"
    if not archive.exists():
        urllib.request.urlretrieve(f"https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_{name}.zip", archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != checksum:
        raise SystemExit("pluginval checksum mismatch; download rejected")
    with zipfile.ZipFile(archive) as source:
        source.extractall(args.directory)
    executable = args.directory / binary
    executable.chmod(0o755)
    print(executable)

if __name__ == "__main__":
    main()
