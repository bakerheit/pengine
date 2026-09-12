#!/usr/bin/env python3
"""Cook the supplied PSX helicopter into Apricot's runtime .emesh + PNG paint.

Unlike every other vehicle in tools/, this one is NOT authored here: it arrives
as a finished low-poly OBJ with its own atlas, so this script converts rather
than models. Keep it a converter. If the silhouette needs changing, change it
in the source and re-run, so the runtime mesh always has a source it came from.

The source is a Y-up OBJ with the nose on +Z, which is already Apricot's
aircraft frame -- no axis swap, and therefore no winding flip either (the
Blender exporters in this directory reverse their triangles only because they
swap Y and Z, which mirrors handedness).

Two objects come out as two meshes on purpose:
  * body.emesh  -- the airframe, opaque.
  * rotor.emesh -- the main rotor disc, a single alpha-cut quad. It is emitted
    RECENTRED ON ITS OWN HUB so the host can spin it about its node's Y axis;
    the hub's offset in body space is printed here and pinned in
    src/city/halberd_helicopter.h. Emitted double-sided: a one-quad rotor seen
    from underneath is back-face culled, and a helicopter is mostly seen from
    underneath.
"""
import argparse
import math
import shutil
import struct
import zipfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / 'assets/models/vehicles/psx_helicopter'
TEXTURE = ROOT / 'assets/textures/vehicles/psx_helicopter'
DEFAULT_SOURCE = MODEL / 'source'


def parse_obj(path):
    """Return {object_name: (vertices, indices)} with per-loop attributes."""
    positions, uvs, normals = [], [], []
    objects, current = {}, None
    for line in path.read_text().splitlines():
        parts = line.split()
        if not parts:
            continue
        tag = parts[0]
        if tag == 'v':
            positions.append(tuple(float(v) for v in parts[1:4]))
        elif tag == 'vt':
            uvs.append(tuple(float(v) for v in parts[1:3]))
        elif tag == 'vn':
            normals.append(tuple(float(v) for v in parts[1:4]))
        elif tag == 'o':
            current = parts[1]
            objects.setdefault(current, [])
        elif tag == 'f':
            if current is None:
                current = 'default'
                objects.setdefault(current, [])
            corners = []
            for token in parts[1:]:
                bits = (token.split('/') + ['', ''])[:3]
                corners.append((
                    int(bits[0]) - 1,
                    int(bits[1]) - 1 if bits[1] else None,
                    int(bits[2]) - 1 if bits[2] else None))
            # Fan-triangulate. These faces are flat quads, so a fan is exact.
            for i in range(1, len(corners) - 1):
                objects[current].append((corners[0], corners[i], corners[i + 1]))
    return objects, positions, uvs, normals


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def normalize(v):
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if length < 1e-12:
        return (0.0, 1.0, 0.0)
    return (v[0] / length, v[1] / length, v[2] / length)


def tangent_for(p, uv, normal):
    """Same construction as tools/vesper_vx91_blender.py, kept bit-compatible."""
    e1, e2 = sub(p[1], p[0]), sub(p[2], p[0])
    du1 = (uv[1][0] - uv[0][0], uv[1][1] - uv[0][1])
    du2 = (uv[2][0] - uv[0][0], uv[2][1] - uv[0][1])
    denominator = du1[0] * du2[1] - du1[1] * du2[0]
    if abs(denominator) > 1e-8:
        t = tuple((e1[i] * du2[1] - e2[i] * du1[1]) / denominator for i in range(3))
        if t[0] * t[0] + t[1] * t[1] + t[2] * t[2] > 1e-10:
            return normalize(t)
    reference = (0.0, 1.0, 0.0)
    if abs(sum(normal[i] * reference[i] for i in range(3))) > 0.9:
        reference = (1.0, 0.0, 0.0)
    return normalize(cross(normal, reference))


def build(triangles, positions, uvs, normals, offset=(0.0, 0.0, 0.0),
          double_sided=False):
    vertices = []
    for tri in triangles:
        windings = [tri, (tri[0], tri[2], tri[1])] if double_sided else [tri]
        for wound, flip in zip(windings, (1.0, -1.0)):
            p = [tuple(positions[c[0]][i] - offset[i] for i in range(3)) for c in wound]
            uv = [uvs[c[1]] if c[1] is not None and c[1] < len(uvs) else (0.0, 0.0)
                  for c in wound]
            n = []
            for c in wound:
                if c[2] is not None and c[2] < len(normals):
                    n.append(tuple(v * flip for v in normals[c[2]]))
                else:
                    n.append(tuple(v * flip for v in normalize(
                        cross(sub(p[1], p[0]), sub(p[2], p[0])))))
            tangent = tangent_for(p, uv, n[0])
            for point, normal, texcoord in zip(p, n, uv):
                vertices.append(point + normalize(normal) + texcoord + tangent + (1.0,))
    return vertices


def write_emesh(path, vertices, material):
    name = (material + '\0').encode()
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('wb') as cooked:
        cooked.write(struct.pack('<8I', 0x48534D45, 2, 0, len(vertices),
                                 len(vertices), 1, len(name), 0))
        for vertex in vertices:
            cooked.write(struct.pack('<12f', *vertex))
        cooked.write(struct.pack(f'<{len(vertices)}I', *range(len(vertices))))
        cooked.write(struct.pack('<4I', 0, len(vertices), 0, 0))
        cooked.write(name)


def bounds(vertices):
    axes = []
    for axis in range(3):
        values = [v[axis] for v in vertices]
        axes.append((min(values), max(values)))
    return axes


def locate_source(given):
    """Accept a directory, an .obj, or the delivered .zip."""
    source = Path(given).expanduser() if given else DEFAULT_SOURCE
    if source.suffix == '.zip':
        DEFAULT_SOURCE.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(source) as archive:
            archive.extractall(DEFAULT_SOURCE)
        source = DEFAULT_SOURCE
    if source.is_dir():
        candidates = sorted(source.glob('*.obj'))
        if not candidates:
            raise SystemExit(f'no .obj under {source}')
        return candidates[0]
    return source


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', default=None,
                        help='source .obj, its directory, or the delivered .zip '
                             f'(default: {DEFAULT_SOURCE})')
    args = parser.parse_args()
    obj = locate_source(args.source)
    objects, positions, uvs, normals = parse_obj(obj)
    print(f'source {obj}')

    body_key = next(k for k in objects if 'rotor' not in k.lower())
    rotor_key = next(k for k in objects if 'rotor' in k.lower())

    body = build(objects[body_key], positions, uvs, normals)
    write_emesh(MODEL / 'body.emesh', body, 'psx_helicopter')
    box = bounds(body)
    print(f'body.emesh  {len(body) // 3:4d} tris  '
          f'x {box[0][0]:.3f}..{box[0][1]:.3f}  '
          f'y {box[1][0]:.3f}..{box[1][1]:.3f}  '
          f'z {box[2][0]:.3f}..{box[2][1]:.3f}')

    # The hub is the centre of the disc's own bounding box, which for a rotor
    # quad is the mast. Print it: src/city/halberd_helicopter.h pins the value
    # and north_airbase_tests checks the mesh still agrees with the constant.
    raw = build(objects[rotor_key], positions, uvs, normals)
    disc = bounds(raw)
    hub = tuple((disc[i][0] + disc[i][1]) * 0.5 for i in range(3))
    rotor = build(objects[rotor_key], positions, uvs, normals, offset=hub,
                  double_sided=True)
    write_emesh(MODEL / 'rotor.emesh', rotor, 'psx_helicopter_rotor')
    span = max(disc[0][1] - disc[0][0], disc[2][1] - disc[2][0])
    print(f'rotor.emesh {len(rotor) // 3:4d} tris  '
          f'hub {hub[0]:.3f} {hub[1]:.3f} {hub[2]:.3f}  span {span:.3f} m')

    TEXTURE.mkdir(parents=True, exist_ok=True)
    for stem, destination in (('texture', 'body.png'), ('rotors', 'rotor.png')):
        found = sorted(obj.parent.glob(f'*{stem}*.png'))
        if not found:
            raise SystemExit(f'no *{stem}*.png beside {obj}')
        with Image.open(found[0]) as image:
            image.convert('RGBA').save(TEXTURE / destination)
        print(f'{destination:10s} <- {found[0].name} {Image.open(found[0]).size}')


if __name__ == '__main__':
    main()
