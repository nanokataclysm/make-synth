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
    parser.add_argument("--platform", choices=("linux-x86_64", "macos-universal", "windows-x86_64"), required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent

    artefacts = [
        args.build / "MakeSynth_artefacts" / "Release",
        args.build / "MakeSynth_artefacts",
        args.build / "Release",
        args.build,
    ]

    vst3_bundle = None
    for art in artefacts:
        for cand in [art / "VST3" / "Make Synth.vst3", art / "Make Synth.vst3"]:
            if cand.exists():
                vst3_bundle = cand
                break
        if vst3_bundle:
            break

    if not vst3_bundle:
        raise SystemExit(f"Missing VST3 bundle in {args.build}")

    args.output.mkdir(parents=True, exist_ok=True)
    name = f"MakeSynth-0.1.0-{args.platform}"
    try:
        commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True, stderr=subprocess.DEVNULL).strip()
    except subprocess.CalledProcessError:
        commit = "local-uncommitted"

    with tempfile.TemporaryDirectory(prefix="make-synth-package-") as temporary:
        package = Path(temporary) / name
        package.mkdir()
        shutil.copytree(vst3_bundle, package / vst3_bundle.name, symlinks=True)

        for art in artefacts:
            au = art / "AU" / "Make Synth.component"
            if au.exists():
                shutil.copytree(au, package / au.name, symlinks=True)
                break

        for art in artefacts:
            for clap in [art / "CLAP" / "Make Synth.clap", art / "Make Synth.clap", args.build / "MakeSynth_CLAP_artefacts" / "Release" / "Make Synth.clap"]:
                if clap.exists():
                    if clap.is_dir():
                        shutil.copytree(clap, package / clap.name, symlinks=True)
                    else:
                        shutil.copy2(clap, package / clap.name)
                    break

        for art in artefacts:
            for sa in [art / "Standalone" / "Make Synth", art / "Standalone" / "Make Synth.app", art / "Standalone" / "Make Synth.exe"]:
                if sa.exists():
                    if sa.is_dir():
                        shutil.copytree(sa, package / sa.name, symlinks=True)
                    else:
                        shutil.copy2(sa, package / sa.name)
                    break

        for document in ("QUICKSTART.md", "README.md", "THIRD_PARTY.md"):
            doc_path = root / document
            if doc_path.exists():
                shutil.copy2(doc_path, package / document)
        if (root / "Licenses").is_dir():
            shutil.copytree(root / "Licenses", package / "Licenses")

        build_meta = {
            "version": "0.1.0",
            "platform": args.platform,
            "commit": commit,
            "juce": "9.0.2",
            "formats": [p.name for p in package.iterdir() if p.name.endswith((".vst3", ".component", ".clap", ".app", ".exe")) or p.name == "Make Synth"]
        }
        (package / "BUILD.json").write_text(json.dumps(build_meta, indent=2) + "\n")

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
