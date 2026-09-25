#!/usr/bin/env python3
"""Check the cooked Ferrone Mast in assets/models/props/ferrone_mast/.

    python3 tools/validate_ferrone_mast.py

ctest cannot run Blender and the cooked files are not tracked, so the suite in
tests/ferrone_mast_tests.cpp checks the pad against the real terrain and the
loader against a synthetic tree, and THIS checks what the cooker actually
wrote: that every mesh parses, faces the way its normals say, stays on the
pad, reaches exactly the landmark height, and that the far silhouette's
texture really is mostly air with steel in it.
"""
import json, struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/models/props/ferrone_mast'
PAD_MIN, PAD_MAX = (-7.0, -7.0), (7.0, 15.0)     # kFerroneMastPad* (engine x, z)
HEIGHT = 60.0                                     # kLandmarks "Ferrone Mast"
failures = []


def check(ok, msg):
    if not ok:
        failures.append(msg)


def read_emesh(path):
    data = path.read_bytes()
    magic, version, _, nv, ni, nsub, nlen, _ = struct.unpack_from('<8I', data, 0)
    check(magic == 0x48534D45 and version == 2, f'{path.name}: bad header')
    verts = [struct.unpack_from('<12f', data, 32 + 48 * i) for i in range(nv)]
    off = 32 + 48 * nv
    idx = struct.unpack_from(f'<{ni}I', data, off)
    return verts, idx


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def main():
    if not (OUT / 'materials.txt').is_file():
        sys.exit(f'no cooked mast under {OUT}; run tools/ferrone_mast_blender.py')
    rows = [l.split() for l in (OUT / 'materials.txt').read_text().splitlines() if l.strip()]
    glow = [l.split() for l in (OUT / 'glow.txt').read_text().splitlines() if l.strip()]
    check(len(glow) == 1, 'glow.txt must name exactly one sphere')
    lo = [1e9] * 3
    hi = [-1e9] * 3
    tris = 0
    lods = set()
    for row in rows + glow:
        mesh, tex, lod = row[0], row[1], int(row[5])
        lods.add(lod)
        check(tex == '-' or (OUT / tex).is_file(), f'{mesh}: missing texture {tex}')
        verts, idx = read_emesh(OUT / mesh)
        check(len(idx) % 3 == 0 and len(idx) > 0, f'{mesh}: empty or ragged')
        agree = 0
        for t in range(0, len(idx), 3):
            a, b, c = (verts[idx[t + k]] for k in range(3))
            g = cross([b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)])
            n = [(a[3 + i] + b[3 + i] + c[3 + i]) for i in range(3)]
            agree += sum(g[i] * n[i] for i in range(3)) >= 0
        # Winding must agree with the normals, or back-face culling eats the
        # face the normal says is the front.
        check(agree >= 0.97 * (len(idx) // 3), f'{mesh}: {len(idx) // 3 - agree} tris wound against their normals')
        tris += len(idx) // 3
        if row in glow:
            continue
        for v in verts:
            for i in range(3):
                lo[i] = min(lo[i], v[i])
                hi[i] = max(hi[i], v[i])
    check(lods == {0, 1, 2, 3}, f'draw sets present: {sorted(lods)}')
    check(abs(hi[1] - HEIGHT) < 0.02, f'top is {hi[1]:.3f} m, the landmark says {HEIGHT}')
    check(lo[1] > -0.5, f'something hangs {lo[1]:.2f} m below the pad')
    check(PAD_MIN[0] <= lo[0] and hi[0] <= PAD_MAX[0], f'x extent {lo[0]:.2f}..{hi[0]:.2f} leaves the pad')
    check(PAD_MIN[1] <= lo[2] and hi[2] <= PAD_MAX[1], f'z extent {lo[2]:.2f}..{hi[2]:.2f} leaves the pad')
    boxes = [list(map(float, l.split())) for l in (OUT / 'collision.txt').read_text().splitlines() if l.strip()]
    for b in boxes:
        check(all(h > 0 for h in b[3:]), f'box {b} has a non-positive half-extent')
        check(PAD_MIN[0] - 1e-3 <= b[0] - b[3] and b[0] + b[3] <= PAD_MAX[0] + 1e-3 and
              PAD_MIN[1] - 1e-3 <= b[2] - b[5] and b[2] + b[5] <= PAD_MAX[1] + 1e-3,
              f'box {b} leaves the pad')
    lights = [l.split() for l in (OUT / 'lights.txt').read_text().splitlines() if l.strip()]
    kinds = [int(l[3]) for l in lights]
    check(kinds.count(2) == 1, 'exactly one flashing L-864 beacon')
    check(kinds.count(1) >= 4, 'L-810 side lights on every leg')
    beacon = next(l for l in lights if l[3] == '2')
    check(float(beacon[1]) > HEIGHT - 1.5, 'the beacon is not at the top')
    manifest = json.loads((OUT / 'manifest.json').read_text())
    check(manifest['triangles'] == tris, 'manifest triangle count disagrees with the meshes')
    try:
        from PIL import Image
        im = Image.open(OUT / 'far-lattice.png')
        alpha = im.getchannel('A').histogram()
        solid = sum(alpha[128:]) / (im.width * im.height)
        # Mostly air, but enough steel to mip down to a visible haze.
        check(0.12 < solid < 0.6, f'far silhouette coverage {solid:.2f} is not a lattice')
    except ImportError:
        print('  (PIL missing: far texture coverage not checked)')
    print(f'ferrone mast: {tris} triangles, {len(rows)} parts, {len(boxes)} boxes, '
          f'{len(lights)} lights, extent x {lo[0]:.2f}..{hi[0]:.2f} y {lo[1]:.2f}..{hi[1]:.2f} '
          f'z {lo[2]:.2f}..{hi[2]:.2f}')
    for f in failures:
        print('FAIL', f)
    sys.exit(1 if failures else 0)


main()
