#!/usr/bin/env python3
"""Stage the licensed PSX Houses 100 surface maps used by Apricot."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT.parent / "resources" / "PSX_Houses_100"
DEFAULT_OUTPUT = ROOT / "assets" / "models" / "world" / "psx_house_textures"
TEXTURES = {
    "Wall_1.png": Path("Textures/Wall/Wall_1.png"),
    "Wall_2.png": Path("Textures/Wall/Wall_2.png"),
    "Wall_3.png": Path("Textures/Wall/Wall_3.png"),
    "Roof_3.png": Path("Textures/Roof/Roof_3.png"),
    "Roof_5.png": Path("Textures/Roof/Roof_5.png"),
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    source = args.source.resolve()
    output = args.output.resolve()
    license_path = source / "License.txt"
    if not license_path.is_file() or "Created by: postdev" not in license_path.read_text():
        raise SystemExit(f"unexpected or missing PSX Houses 100 license: {license_path}")

    output.mkdir(parents=True, exist_ok=True)
    copied = []
    for name, relative in TEXTURES.items():
        source_path = source / relative
        if not source_path.is_file():
            raise SystemExit(f"missing source texture: {source_path}")
        shutil.copy2(source_path, output / name)
        copied.append(name)

    report = {
        "creator": "postdev",
        "source": str(source),
        "textures": copied,
    }
    (output / "stage-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"staged {len(copied)} PSX house textures in {output}")


if __name__ == "__main__":
    main()
