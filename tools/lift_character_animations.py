#!/usr/bin/env python3
"""Lift the cooked PSX character clips out of Probable Cause into apricot.

WHY THIS IS A COPY AND NOT A COOK. Characters_psx 1.1 changed the FBX rest-space
axes without changing its mixamorig bone names, so re-cooking a locomotion clip
from the newer revision produces a bind pose that looks fine and animation poses
rotated onto their back. tools/cook_character_assets.py says the same thing
about the meshes and skeletons, and for the same reason. Probable Cause already
holds these clips cooked in the exact rest space our staged skeletons use, so
the safe operation is a verified copy.

Verification here is not "the file exists". Every source is parsed as an .eanim
(src/core/anim_format.h) before anything is published: magic, version, a
non-empty channel table, a finite positive duration, and a payload whose size is
exactly what the header says it should be. A clip that fails any of those aborts
the whole run, because a half-published animation set is worse than none - the
runtime would load fifteen clips, fail on the sixteenth, and report it as a rig
problem.

Everything written stays under assets/models/, which is gitignored
(.gitignore: "Proprietary model assets must stay local"). Nothing this script
produces is committed; re-run it on a fresh clone.

    python3 tools/lift_character_animations.py
    python3 tools/lift_character_animations.py --check   # verify, publish nothing
"""

from __future__ import annotations

import argparse
import math
import shutil
import struct
import tempfile
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRIVATE_ANIMATIONS = ROOT / "assets/models/characters/psx_pack/animations"

# src/core/anim_format.h
EANIM_MAGIC = 0x4D4E4145  # 'EANM'
EANIM_VERSION = 1
HEADER_STRUCT = struct.Struct("<IIIIf12x")   # 32 bytes
CHANNEL_STRUCT = struct.Struct("<IIII")      # 16 bytes
VEC3_KEY_BYTES = 16
QUAT_KEY_BYTES = 20

assert HEADER_STRUCT.size == 32
assert CHANNEL_STRUCT.size == 16


@dataclass(frozen=True)
class Clip:
    """One published clip: the apricot name, and the Probable Cause file."""
    name: str
    source: str
    role: str


# The runtime name on the left is what src/app/character_animation.h registers.
# Keep idle/walk/sprint spelled exactly as they were: they are already staged
# under those names and character_visual.cpp loads them by that path.
CLIPS = (
    Clip("idle",         "breathing_idle.eanim",  "ambient idle"),
    Clip("walk",         "walking.eanim",         "locomotion"),
    Clip("sprint",       "sprint.eanim",          "locomotion"),
    Clip("pistol_idle",  "pistol_idle.eanim",     "armed idle"),
    Clip("pistol_walk",  "pistol_walk.eanim",     "armed locomotion"),
    Clip("pistol_run",   "pistol_run.eanim",      "armed locomotion"),
    Clip("punch_left",   "punch_left.eanim",      "melee"),
    Clip("punch_right",  "punch_right.eanim",     "melee"),
    Clip("hit_by_car",   "hit_by_car.eanim",      "flinch / knockdown"),
    Clip("die_forward",  "dying_forwards.eanim",  "death"),
    Clip("die_backward", "dying_backwards.eanim", "death"),
    Clip("stand_up",     "stand_up.eanim",        "get up"),
    Clip("jump",         "jump.eanim",            "air"),
)


@dataclass(frozen=True)
class ClipFacts:
    channels: int
    duration: float
    keys: int
    size: int


def inspect(path: Path) -> ClipFacts:
    """Parse the whole .eanim and return what it actually contains.

    Raises RuntimeError on anything the C++ loader would also reject, plus the
    exact-size check the loader gets for free from its trailing-byte test.
    """
    payload = path.read_bytes()
    if len(payload) < HEADER_STRUCT.size:
        raise RuntimeError(f"{path}: shorter than an .eanim header")
    magic, version, channel_count, string_block, duration = HEADER_STRUCT.unpack_from(
        payload, 0)
    if magic != EANIM_MAGIC:
        raise RuntimeError(f"{path}: bad magic 0x{magic:08X}")
    if version != EANIM_VERSION:
        raise RuntimeError(f"{path}: version {version}, expected {EANIM_VERSION}")
    if channel_count == 0:
        raise RuntimeError(f"{path}: no channels")
    if string_block == 0:
        raise RuntimeError(f"{path}: empty bone-name block")
    if not math.isfinite(duration) or duration <= 0.0:
        raise RuntimeError(f"{path}: duration {duration!r} is not usable")

    offset = HEADER_STRUCT.size
    channels = []
    for index in range(channel_count):
        if offset + CHANNEL_STRUCT.size > len(payload):
            raise RuntimeError(f"{path}: channel table truncated at {index}")
        channels.append(CHANNEL_STRUCT.unpack_from(payload, offset))
        offset += CHANNEL_STRUCT.size

    keys = 0
    for name_offset, pos, rot, scale in channels:
        if name_offset >= string_block:
            raise RuntimeError(f"{path}: bone name offset {name_offset} out of range")
        offset += pos * VEC3_KEY_BYTES + rot * QUAT_KEY_BYTES + scale * VEC3_KEY_BYTES
        keys += pos + rot + scale
    offset += string_block
    if offset != len(payload):
        raise RuntimeError(
            f"{path}: header describes {offset} bytes, file is {len(payload)}")
    return ClipFacts(channel_count, duration, keys, len(payload))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--probable-cause-assets", type=Path,
        default=Path("/Users/andrewbaker/workspace/Games/probablecause/assets"),
        help="Probable Cause asset root holding the proven cooked clips",
    )
    parser.add_argument(
        "--destination", type=Path, default=PRIVATE_ANIMATIONS,
        help="where to publish (default: the gitignored apricot animations dir)",
    )
    parser.add_argument(
        "--check", action="store_true",
        help="parse and report every source clip, then publish nothing",
    )
    args = parser.parse_args()

    source_root = args.probable_cause_assets / "models/characters"
    if not source_root.is_dir():
        raise SystemExit(f"Probable Cause character assets not found: {source_root}")

    # Stage and verify EVERY clip before publishing ANY of it.
    with tempfile.TemporaryDirectory(prefix="apricot-anim-lift-") as name:
        stage = Path(name)
        facts: dict[str, ClipFacts] = {}
        for clip in CLIPS:
            source = source_root / clip.source
            if not source.is_file():
                raise SystemExit(f"clip not found: {source}")
            try:
                facts[clip.name] = inspect(source)
            except RuntimeError as error:
                raise SystemExit(str(error)) from error
            if not args.check:
                shutil.copy2(source, stage / f"{clip.name}.eanim")

        if not args.check:
            args.destination.mkdir(parents=True, exist_ok=True)
            for clip in CLIPS:
                shutil.copy2(stage / f"{clip.name}.eanim",
                             args.destination / f"{clip.name}.eanim")

    total_keys = 0
    total_bytes = 0
    for clip in CLIPS:
        fact = facts[clip.name]
        total_keys += fact.keys
        total_bytes += fact.size
        print(f"CLIP name={clip.name:<12} role={clip.role:<18} "
              f"source={clip.source:<22} "
              f"channels={fact.channels:<3} duration={fact.duration:7.3f}s "
              f"keys={fact.keys:<6} bytes={fact.size}")
    print(f"ANIM_LIFT clips={len(CLIPS)} keys={total_keys} bytes={total_bytes} "
          f"{'verified-only' if args.check else 'output=' + str(args.destination)}")
    print("NOTE assets/models/ is gitignored; this output is local-only.")


if __name__ == "__main__":
    main()
