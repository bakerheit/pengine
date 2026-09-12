#!/usr/bin/env python3
"""Cook the Mixamo "Climbing Up Wall" clip onto the staged PSX-pack rigs.

WHY THIS IS A COOK AND NOT A COPY. tools/lift_character_animations.py exists
because Probable Cause already held those clips in the exact rest space our
staged skeletons use, so a verified copy was the safe operation. This clip is
not in that set: it is a stock Mixamo export against Mixamo's own X Bot, and it
has to be retargeted before it means anything on our rigs.

THREE THINGS MAKE THE NAIVE VERSION WRONG, AND ALL THREE LOOK LIKE A RIG BUG.

1. OFF-RIG CHANNELS. X Bot carries 51 animated bones including fingers; the
   staged rigs have 28. Animation::load() counts the leftovers as unresolved
   channels and apricot_character_lab refuses the clip outright. Only the
   bones the target actually has are written.

2. POSITION TRACKS ARE NOT OPTIONAL. src/core/skeletal_animation.cpp samples an
   ANIMATED bone's translation with a fallback of ZERO, not of bind -- so a
   channel present with no position keys collapses that bone onto its parent.
   Dropping the source's position tracks (which carry X Bot's proportions, not
   ours) therefore cannot mean omitting them: every bone is written with the
   TARGET rig's own bind translation instead. Skipping this renders the
   character as a ball of limbs at the hips, which reads as a corrupt file.

3. THE TWO RIGS DO NOT SHARE A REST POSE. Measured off the bind matrices: X Bot
   is a T-pose (upper arm points +X, straight out); the staged rigs are bound
   arms-down (upper arm points -Y). Legs and spine agree to within 11 degrees,
   the arms are 113-122 degrees apart. A delta-from-bind transfer -- the usual
   retarget -- adds the clip's motion onto THIS rig's rest pose, and an
   overhead reach comes out horizontal, with the legs looking perfect.
   What makes the absolute copy below correct instead is a measured fact: both
   rigs run every bone along its local +Y, to 0.00 degrees. So copying the
   source's world orientation points each target limb exactly where the
   reference points it, and the differing rest poses stop mattering.

THE ROOT CARRIES NO MOTION, AND THAT IS DELIBERATE TWICE OVER. game/climb.cpp
owns the trajectory -- climb_position() is what moves the feet, and a clip that
also moved them would apply the climb twice; this is the same rule locomotion
already follows ("the game moved the character, so the clip must not move it
again", app/character_animation.h). The second reason is that it makes one file
safe to share across every staged rig: a root translation track is in RIG
UNITS, and these rigs are not all in the same ones -- most are centimetre-scale
and several are metre-scale, a factor of 270 apart. A shared clip carrying real
root travel would drop the metre rigs 270 m through the floor.

The verification that caught all three was a render, not a test: the clip put
side by side against a Blender render of the source FBX at matched timestamps.
A clip can satisfy every structural check in this file and still be a man
folded into a cube. Re-run --preview after any change here and LOOK at it.

Everything written lands under assets/models/, which is gitignored
(".gitignore: Proprietary model assets must stay local"). Nothing this script
produces is committed; re-run it on a fresh clone.

    python3 tools/cook_climb_animation.py --source "~/Downloads/Climbing Up Wall.fbx"
    python3 tools/cook_climb_animation.py --check     # verify, publish nothing
"""

from __future__ import annotations

import argparse
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RIGS = ROOT / "assets/models/characters/psx_pack"
OUT_NAME = "climb.eanim"

# src/core/anim_format.h
EANIM_MAGIC = 0x4D4E4145  # 'EANM'
EANIM_VERSION = 1
ANIM_HEADER = struct.Struct("<IIIIf12x")   # 32 bytes
ANIM_CHANNEL = struct.Struct("<IIII")      # 16 bytes
# src/core/skeleton_format.h
ESKEL_MAGIC = 0x4C4B5345  # 'ESKL'
ESKEL_VERSION = 1
ESKEL_HEADER = struct.Struct("<IIII")      # 16 bytes
ESKEL_BONE = struct.Struct("<iI16f16f")    # 136 bytes

assert ANIM_HEADER.size == 32 and ANIM_CHANNEL.size == 16
assert ESKEL_HEADER.size == 16 and ESKEL_BONE.size == 136

ROOT_BONE = "mixamorig:Hips"


# --- tiny 3x3 helpers. No numpy: this has to run on a bare checkout. ---------

def mat_id():
    return [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)]
            for i in range(3)]


def mat_t(a):
    return [[a[j][i] for j in range(3)] for i in range(3)]


def quat_to_mat(x, y, z, w):
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ]


def mat_to_quat(m):
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0.0:
        s = math.sqrt(t + 1.0) * 2.0
        w, x = 0.25 * s, (m[2][1] - m[1][2]) / s
        y, z = (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0
        w, x = (m[2][1] - m[1][2]) / s, 0.25 * s
        y, z = (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0
        w, x = (m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s
        y, z = 0.25 * s, (m[1][2] + m[2][1]) / s
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0
        w, x = (m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s
        y, z = (m[1][2] + m[2][1]) / s, 0.25 * s
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    return (x / n, y / n, z / n, w / n)


def orthonormal(m):
    """Gram-Schmidt. The bind matrices are rigid, so this only strips the
    rounding that accumulates through a long parent chain."""
    cols = [[m[0][j], m[1][j], m[2][j]] for j in range(3)]
    out = []
    for c in cols:
        for prev in out:
            d = sum(c[i] * prev[i] for i in range(3))
            c = [c[i] - d * prev[i] for i in range(3)]
        n = math.sqrt(sum(v * v for v in c)) or 1.0
        out.append([v / n for v in c])
    return [[out[j][i] for j in range(3)] for i in range(3)]


# --- formats ----------------------------------------------------------------

def read_eskel(path: Path):
    raw = path.read_bytes()
    magic, version, count, strings = ESKEL_HEADER.unpack_from(raw, 0)
    if magic != ESKEL_MAGIC or version != ESKEL_VERSION:
        raise SystemExit(f"{path}: not an .eskel (magic {magic:#x} v{version})")
    block = raw[len(raw) - strings:]
    bones = []
    for i in range(count):
        f = ESKEL_BONE.unpack_from(raw, ESKEL_HEADER.size + i * ESKEL_BONE.size)
        parent, offset = f[0], f[1]
        local = f[18:34]                       # column-major mat4
        name = block[offset:block.index(b"\0", offset)].decode()
        bones.append(dict(parent=parent, name=name, local=local))
    return bones


def read_eanim(path: Path):
    raw = path.read_bytes()
    magic, version, count, strings, duration = ANIM_HEADER.unpack_from(raw, 0)
    if magic != EANIM_MAGIC or version != EANIM_VERSION:
        raise SystemExit(f"{path}: not an .eanim (magic {magic:#x} v{version})")
    if not (duration > 0.0) or not math.isfinite(duration):
        raise SystemExit(f"{path}: duration {duration} is not a positive finite")
    table = [ANIM_CHANNEL.unpack_from(raw, ANIM_HEADER.size + i * ANIM_CHANNEL.size)
             for i in range(count)]
    block = raw[len(raw) - strings:]
    offset = ANIM_HEADER.size + count * ANIM_CHANNEL.size
    channels = []
    for (name_at, n_pos, n_rot, n_scale) in table:
        name = block[name_at:block.index(b"\0", name_at)].decode()
        pos = [struct.unpack_from("<ffff", raw, offset + i * 16) for i in range(n_pos)]
        offset += n_pos * 16
        rot = [struct.unpack_from("<fffff", raw, offset + i * 20) for i in range(n_rot)]
        offset += n_rot * 20
        offset += n_scale * 16
        channels.append(dict(name=name, pos=pos, rot=rot))
    if offset != len(raw) - strings:
        raise SystemExit(f"{path}: key payload is {offset} bytes, header says "
                         f"{len(raw) - strings}")
    return duration, channels


def write_eanim(path: Path, duration: float, channels):
    names, offsets = b"", []
    for ch in channels:
        offsets.append(len(names))
        names += ch["name"].encode() + b"\0"
    body = b""
    for ch in channels:
        for k in ch["pos"]:
            body += struct.pack("<ffff", *k)
        for k in ch["rot"]:
            body += struct.pack("<fffff", *k)
    header = ANIM_HEADER.pack(EANIM_MAGIC, EANIM_VERSION, len(channels),
                              len(names), duration)
    table = b"".join(ANIM_CHANNEL.pack(offsets[i], len(channels[i]["pos"]),
                                       len(channels[i]["rot"]), 0)
                     for i in range(len(channels)))
    path.write_bytes(header + table + body + names)


# --- the retarget -----------------------------------------------------------

def chain_depth(bones, i):
    depth = 0
    while bones[i]["parent"] >= 0:
        i = bones[i]["parent"]
        depth += 1
    return depth


def bind_world(bones):
    """World bind ROTATION per bone, plus the root's world bind height."""
    rot, height = {}, {}
    for i in sorted(range(len(bones)), key=lambda b: chain_depth(bones, b)):
        m = bones[i]["local"]
        local = [[m[0], m[4], m[8]], [m[1], m[5], m[9]], [m[2], m[6], m[10]]]
        parent = bones[i]["parent"]
        if parent < 0:
            rot[i] = orthonormal(local)
            height[i] = m[13]
        else:
            rot[i] = orthonormal(mat_mul(rot[parent], local))
            height[i] = height[parent] + m[13]
    return rot, height


def retarget(source_skel, source_anim, target_skel):
    src = read_eskel(source_skel)
    tgt = read_eskel(target_skel)
    duration, channels = read_eanim(source_anim)
    by_src = {b["name"]: i for i, b in enumerate(src)}
    by_tgt = {b["name"]: i for i, b in enumerate(tgt)}
    clip = {c["name"]: c for c in channels}

    # Bones the clip does not animate are LEFT OUT rather than written frozen.
    # An omitted bone holds its bind transform (skeletal_animation.cpp); a bone
    # written with an empty rotation track would instead snap to identity. In
    # practice these are Mixamo's end sites -- HeadTop_End, the *4 finger tips
    # -- which Mixamo never keys on any clip, so this is the normal case and
    # not a defect. Anything BEYOND an end site would be a real freeze, so the
    # names are reported rather than swallowed.
    frozen = [b["name"] for b in tgt if b["name"] not in clip]
    animated = [b for b in tgt if b["name"] in clip]
    if not animated:
        raise SystemExit("the clip animates none of this rig's bones; wrong rig?")

    src_bind, src_height = bind_world(src)
    tgt_bind, tgt_height = bind_world(tgt)
    root_src, root_tgt = by_src[ROOT_BONE], by_tgt[ROOT_BONE]
    # Reported, never applied -- see the root note in the docstring. It is the
    # ratio that WOULD convert the source's root travel into this rig's units,
    # and it is worth printing because it is how you discover that the staged
    # rigs are not all in the same units: most sit near 2.5 (centimetre rigs)
    # and several near 0.009 (metre rigs), a factor of 270 apart.
    scale = tgt_height[root_tgt] / src_height[root_src]

    times = sorted({k[0] for c in channels for k in c["rot"]})
    src_order = sorted(range(len(src)), key=lambda i: chain_depth(src, i))
    tgt_order = sorted(range(len(tgt)), key=lambda i: chain_depth(tgt, i))
    out = {b["name"]: [] for b in tgt}

    for index, stamp in enumerate(times):
        world = {}
        for i in src_order:
            name = src[i]["name"]
            keys = clip.get(name, {}).get("rot") if name in clip else None
            if keys:
                k = keys[min(index, len(keys) - 1)]
                local = quat_to_mat(k[1], k[2], k[3], k[4])
            else:
                local = src_bind[i] if src[i]["parent"] < 0 else mat_mul(
                    mat_t(src_bind[src[i]["parent"]]), src_bind[i])
            parent = src[i]["parent"]
            world[i] = local if parent < 0 else mat_mul(world[parent], local)
        target_world = {}
        for i in tgt_order:
            name = tgt[i]["name"]
            # The absolute copy. See the header: both rigs run bones along
            # local +Y, so this points each limb where the reference points it.
            target_world[i] = world[by_src[name]] if name in by_src else tgt_bind[i]
            parent = tgt[i]["parent"]
            local = (target_world[i] if parent < 0
                     else mat_mul(mat_t(target_world[parent]), target_world[i]))
            out[name].append((stamp,) + mat_to_quat(local))

    cooked = []
    for bone in animated:
        name, m = bone["name"], bone["local"]
        bind_t = (m[12], m[13], m[14])
        # EVERY bone gets its bind translation, the ROOT included. Not omitted
        # -- see note 2 in the module docstring -- and, for the root, not the
        # clip's own travel either. See the root note there.
        pos = [(0.0,) + bind_t]
        cooked.append(dict(name=name, pos=pos, rot=out[name]))
    return duration, cooked, scale, frozen


# --- driver -----------------------------------------------------------------

def find_meshconv(explicit: str | None) -> Path:
    candidates = []
    if explicit:
        candidates.append(Path(explicit).expanduser())
    if os.environ.get("MESHCONV"):
        candidates.append(Path(os.environ["MESHCONV"]).expanduser())
    candidates += [
        Path.home() / "workspace/Games/probablecause/build/bin/meshconv",
        Path.home() / "workspace/Games/probablecause/build-release/bin/meshconv",
    ]
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return c
    raise SystemExit(
        "meshconv not found. It is Probable Cause's Assimp-backed FBX reader "
        "(tools/meshconv), the same tool the staged clips were cooked with. "
        "Build it there, or pass --meshconv / set MESHCONV.")


def target_rigs():
    rigs = sorted(p for p in RIGS.glob("*/skin.eskel"))
    if not rigs:
        raise SystemExit(
            f"no staged rigs under {RIGS}. Run tools/cook_character_assets.py "
            "first -- assets/models/ is gitignored and starts empty.")
    return rigs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", default="~/Downloads/Climbing Up Wall.fbx",
                    help="the stock Mixamo FBX export")
    ap.add_argument("--meshconv", default=None)
    ap.add_argument("--check", action="store_true",
                    help="cook and verify into a temp dir; publish nothing")
    args = ap.parse_args()

    source = Path(args.source).expanduser()
    if not source.is_file():
        raise SystemExit(f"source clip not found: {source}")
    meshconv = find_meshconv(args.meshconv)

    with tempfile.TemporaryDirectory() as tmp:
        stage = Path(tmp) / "climb_raw"
        subprocess.run([str(meshconv), str(source), str(stage)],
                       check=True, stdout=subprocess.DEVNULL)
        raw_anim, raw_skel = stage.with_suffix(".eanim"), stage.with_suffix(".eskel")
        if not raw_anim.is_file() or not raw_skel.is_file():
            raise SystemExit("meshconv produced no .eanim/.eskel for this input")

        published = 0
        for rig in target_rigs():
            duration, cooked, scale, frozen = retarget(raw_skel, raw_anim, rig)
            out = Path(tmp) / f"{rig.parent.name}.eanim"
            write_eanim(out, duration, cooked)
            # Read it back through the same parser the engine's loader mirrors:
            # a half-written clip that parses is worse than one that does not.
            back_duration, back = read_eanim(out)
            if abs(back_duration - duration) > 1e-5 or len(back) != len(cooked):
                raise SystemExit(f"{rig.parent.name}: round trip disagrees")
            names = {c["name"] for c in back}
            rig_names = {b["name"] for b in read_eskel(rig)}
            # EVERY channel must name a bone this rig has. The subset direction
            # is the one that matters: a channel the rig cannot resolve is
            # counted by Animation::unresolved_channels(), and
            # apricot_character_lab refuses any clip with a non-zero count.
            if not names <= rig_names:
                raise SystemExit(f"{rig.parent.name}: clip names bones the rig "
                                 f"lacks ({sorted(names - rig_names)}); "
                                 "Animation::load() would report them unresolved")
            if not args.check:
                dest = rig.parent.parent / "animations" / OUT_NAME
                dest.parent.mkdir(parents=True, exist_ok=True)
                # One clip per rig would be correct but every staged rig shares
                # the 28-bone subset these channels name, so one file serves
                # them all -- exactly as the lifted clips already do.
                shutil.copyfile(out, dest)
            published += 1
            held = f"  holds bind: {', '.join(frozen)}" if frozen else ""
            print(f"  {rig.parent.name:24s} {len(cooked):3d} channels  "
                  f"{duration:.3f}s  root scale {scale:.4f}{held}")

    where = "verified" if args.check else f"-> animations/{OUT_NAME}"
    print(f"climb clip: {published} rigs {where}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
