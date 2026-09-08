#!/usr/bin/env python3
"""Stage the user's supplied police uniforms on the proven police body rig.

The Character_17 police body is already cooked in the rest space expected by
the game's shared clips. Character_19 supplies a compatible tan shirt/pants
atlas; it is a second uniform skin on that same body, not a claim that its
different FBX hair geometry was imported. Character_18/20 have a different UV
layout and must not be substituted. No source or cooked asset leaves the
ignored assets/models directory.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PRIVATE_ROOT = ROOT / "assets/models/characters/psx_pack"
VARIANTS = (("police_male_17", "Character_17_Police.png"),
            ("police_male_19", "Character_19_Police.png"))


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-pack", type=Path,
        default=Path.home() / "workspace/Games/resources/Characters_psx_01",
        help="extracted user-supplied character pack",
    )
    parser.add_argument(
        "--rig-dir", type=Path,
        default=PRIVATE_ROOT / "civilian_male_17_police",
        help="proven police rig staged by cook_character_assets.py",
    )
    args = parser.parse_args()
    geometry = args.source_pack / "Models/Rig/Male/Character_17_Police.fbx"
    required = [geometry, args.rig_dir / "skin.emesh",
                args.rig_dir / "skin.eskel"]
    required += [args.source_pack / "Textures" / texture
                 for _, texture in VARIANTS]
    required += [PRIVATE_ROOT / "animations" / f"{clip}.eanim"
                 for clip in ("idle", "walk", "sprint")]
    for source in required:
        if not source.is_file():
            raise SystemExit(f"required supplied police asset missing: {source}")

    with tempfile.TemporaryDirectory(prefix="apricot-police-stage-") as name:
        stage = Path(name)
        for variant, texture_name in VARIANTS:
            destination = stage / variant
            destination.mkdir()
            texture = args.source_pack / "Textures" / texture_name
            for suffix in ("emesh", "eskel"):
                shutil.copy2(args.rig_dir / f"skin.{suffix}",
                             destination / f"skin.{suffix}")
            shutil.copy2(texture, destination / "body.png")
            manifest = {
                "variant": variant,
                "body_source": str(geometry),
                "body_source_sha256": sha256(geometry),
                "proven_runtime_rig": str(args.rig_dir),
                "texture_source": str(texture),
                "texture_source_sha256": sha256(texture),
                "skin_emesh_sha256": sha256(destination / "skin.emesh"),
                "skin_eskel_sha256": sha256(destination / "skin.eskel"),
                "standing_height_m": 1.76,
                "note": "Both uniform skins share the proven Character_17 police body.",
            }
            (destination / "provenance.json").write_text(
                json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

        # Validate every input before publishing either uniform.
        for variant, texture_name in VARIANTS:
            destination = PRIVATE_ROOT / variant
            destination.mkdir(parents=True, exist_ok=True)
            for source in (stage / variant).iterdir():
                shutil.copy2(source, destination / source.name)
            print(f"POLICE_CHARACTER name={variant} body=Character_17_Police "
                  f"texture={texture_name} height_m=1.76 output={destination}")


if __name__ == "__main__":
    main()
