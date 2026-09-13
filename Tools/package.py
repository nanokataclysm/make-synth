#!/usr/bin/env python3
"""Package the VST3 bundle, installation guide, and dependency notices."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build"))
    parser.add_argument("--output", type=Path, default=Path("dist"))
    parser.add_argument("--platform", choices=("linux-x86_64", "macos-universal"), required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    bundle = args.build / "MakeSynth_artefacts/Release/VST3/Make Synth.vst3"
    if not bundle.is_dir():
        raise SystemExit(f"Missing VST3 bundle: {bundle}")
    args.output.mkdir(parents=True, exist_ok=True)
    name = f"MakeSynth-0.1.0-{args.platform}"
    try:
        commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True, stderr=subprocess.DEVNULL).strip()
    except subprocess.CalledProcessError:
        commit = "local-uncommitted"
    with tempfile.TemporaryDirectory(prefix="make-synth-package-") as temporary:
        package = Path(temporary) / name
        package.mkdir()
        shutil.copytree(bundle, package / bundle.name, symlinks=True)
        for document in ("QUICKSTART.md", "THIRD_PARTY.md"):
            shutil.copy2(root / document, package / document)
        shutil.copytree(root / "Licenses", package / "Licenses")
        (package / "BUILD.json").write_text(json.dumps({"version": "0.1.0", "platform": args.platform, "commit": commit, "juce": "9.0.2"}, indent=2) + "\n")
        archive = args.output / f"{name}.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
            for file in sorted(package.rglob("*")):
                if file.is_symlink():
                    raise SystemExit(f"Unexpected symlink in bundle: {file}")
                output.write(file, file.relative_to(temporary))
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        (args.output / f"{name}.zip.sha256").write_text(f"{digest}  {archive.name}\n")
        print(archive)
        print(digest)

if __name__ == "__main__":
    main()
