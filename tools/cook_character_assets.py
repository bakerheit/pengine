#!/usr/bin/env python3
"""Stage the supplied PSX cast with Probable Cause's proven rig data.

Characters_psx 1.1 changed the FBX rest-space axes without changing its
mixamorig bone names. Feeding that revision to the old mesh cooker makes the
bind pose look fine but rotates every locomotion pose onto its back. The
original Probable Cause tree already contains runtime meshes and skeletons
cooked from the same character pack in the exact space used by its animations.

This tool pairs those proven meshes/skeletons/animations with the exact texture
members from the user's ZIP. Everything it publishes remains below the ignored
assets/models boundary.
"""

from __future__ import annotations

import argparse
import shutil
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PRIVATE_ROOT = ROOT / "assets/models/characters/psx_pack"
PRIVATE_ANIMATIONS = PRIVATE_ROOT / "animations"


@dataclass(frozen=True)
class PackedCharacter:
    name: str
    runtime_stem: str
    texture_member: str


CHARACTERS = (
    PackedCharacter(
        "player_male_01", "character_01",
        "Characters_psx_01/Textures/Character_01.png",
    ),
    PackedCharacter(
        "civilian_male_03", "ped_03",
        "Characters_psx_01/Textures/Character_03.png",
    ),
    PackedCharacter(
        "civilian_male_05", "ped_05",
        "Characters_psx_01/Textures/Character_05.png",
    ),
    PackedCharacter(
        "civilian_male_07", "ped_07",
        "Characters_psx_01/Textures/Character_07.png",
    ),
    PackedCharacter(
        "civilian_male_09", "ped_09",
        "Characters_psx_01/Textures/Character_09.png",
    ),
    PackedCharacter(
        "civilian_female_03", "ped_f03",
        "Characters_psx_01/Textures/Character_Female_03.png",
    ),
    PackedCharacter(
        "civilian_female_05", "ped_f05",
        "Characters_psx_01/Textures/Character_Female_05.png",
    ),
    PackedCharacter(
        "civilian_female_07", "ped_f07",
        "Characters_psx_01/Textures/Character_Female_07.png",
    ),
    PackedCharacter(
        "civilian_female_09", "ped_f09",
        "Characters_psx_01/Textures/Character_Female_09.png",
    ),
    # These supplied skins share the matching proven PSX body UVs. Each pairing
    # is screened in Character Lab before joining the runtime pool.
    PackedCharacter(
        "civilian_male_06", "ped_05",
        "Characters_psx_01/Textures/Character_06.png",
    ),
    PackedCharacter(
        "civilian_male_08", "ped_07",
        "Characters_psx_01/Textures/Character_08.png",
    ),
    PackedCharacter(
        "civilian_male_10", "ped_09",
        "Characters_psx_01/Textures/Character_10.png",
    ),
    PackedCharacter(
        "civilian_male_11", "ped_09",
        "Characters_psx_01/Textures/Character_11.png",
    ),
    PackedCharacter(
        "civilian_male_13", "ped_14",
        "Characters_psx_01/Textures/Character_13.png",
    ),
    PackedCharacter(
        "civilian_male_14", "ped_14",
        "Characters_psx_01/Textures/Character_14.png",
    ),
    PackedCharacter(
        "civilian_male_15", "ped_14",
        "Characters_psx_01/Textures/Character_15.png",
    ),
    PackedCharacter(
        "civilian_male_17_police", "ped_17_police",
        "Characters_psx_01/Textures/Character_17_Police.png",
    ),
    PackedCharacter(
        "civilian_female_04", "ped_f03",
        "Characters_psx_01/Textures/Character_Female_04.png",
    ),
    PackedCharacter(
        "civilian_female_14", "ped_f14",
        "Characters_psx_01/Textures/Character_Female_14.png",
    ),
)

ANIMATIONS = {
    "idle": "breathing_idle.eanim",
    "walk": "walking.eanim",
    "sprint": "sprint.eanim",
}


def extract_member(archive: Path, member: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as package:
        try:
            info = package.getinfo(member)
        except KeyError as exc:
            raise RuntimeError(f"{archive} does not contain {member}") from exc
        with package.open(info) as source, destination.open("wb") as output:
            shutil.copyfileobj(source, output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--main-archive", type=Path,
        default=Path("/Users/andrewbaker/Downloads/Characters_psx_1.1.zip"),
    )
    parser.add_argument(
        "--probable-cause-assets", type=Path,
        default=Path(
            "/Users/andrewbaker/workspace/Games/probablecause/assets"
        ),
        help="original game's asset root containing its proven runtime rigs",
    )
    args = parser.parse_args()

    if not args.main_archive.is_file():
        raise SystemExit(f"character archive not found: {args.main_archive}")
    runtime_root = args.probable_cause_assets / "models/characters"

    with tempfile.TemporaryDirectory(prefix="apricot-character-stage-") as name:
        stage = Path(name)
        for character in CHARACTERS:
            destination = stage / character.name
            destination.mkdir(parents=True)
            for suffix in (".emesh", ".eskel"):
                source = runtime_root / f"{character.runtime_stem}{suffix}"
                if not source.is_file():
                    raise SystemExit(f"runtime rig not found: {source}")
                shutil.copy2(source, destination / f"skin{suffix}")
            extract_member(
                args.main_archive, character.texture_member,
                destination / "body.png",
            )

        staged_animations = stage / "animations"
        staged_animations.mkdir()
        for destination_name, source_name in ANIMATIONS.items():
            source = runtime_root / source_name
            if not source.is_file():
                raise SystemExit(f"animation not found: {source}")
            shutil.copy2(source, staged_animations / f"{destination_name}.eanim")

        # Publish only after every source has been checked and staged.
        PRIVATE_ANIMATIONS.mkdir(parents=True, exist_ok=True)
        for source in staged_animations.iterdir():
            shutil.copy2(source, PRIVATE_ANIMATIONS / source.name)
        for character in CHARACTERS:
            source = stage / character.name
            destination = PRIVATE_ROOT / character.name
            destination.mkdir(parents=True, exist_ok=True)
            for filename in ("skin.emesh", "skin.eskel", "body.png"):
                shutil.copy2(source / filename, destination / filename)
            print(
                f"CHARACTER_SKIN name={character.name} "
                f"runtime={character.runtime_stem} texture={character.texture_member}"
            )

    print(
        f"CHARACTER_STAGE models={len(CHARACTERS)} "
        f"animations={','.join(ANIMATIONS)} output={PRIVATE_ROOT}"
    )


if __name__ == "__main__":
    main()
