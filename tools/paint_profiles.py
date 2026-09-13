#!/usr/bin/env python3
"""Generate the C++ side of vehicle paint from tools/paint_lab.py.

  paint_profiles.py golden [--check]

golden  Runs the lab on synthetic 16x16 atlases and writes
        tests/vehicle_paint_golden.inc: the params, the pixels in both row
        orders, and the lab's mask, trust, base colour and recolours.
        vehicle_paint_tests holds src/game/vehicle_paint.cpp to it within one
        8-bit level. --check exits 1 if the committed file is stale.

Emitter rules. Each one is a -Werror failure somebody already paid for:
  - every float is printed %.5f with an f suffix: a double literal in a float
    field fails -Wimplicit-float-conversion
  - an empty list is `nullptr, 0`, never `{}`: a zero-length array fails
    -Wzero-length-array
  - counts are static_cast<uint16_t>(std::size(kName))
  - identifiers, numbers and the case names in GOLDEN_CASES, nothing else: the
    sim purity guard does not scan .inc files, so the generator is the only
    thing keeping them clean
The lab runs on the float32 value of each printed literal, so the numbers in
the file are exactly the numbers that were tested.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))
import paint_lab as lab  # noqa: E402

GOLDEN = REPO / 'tests/vehicle_paint_golden.inc'
GOLDEN_TARGETS = [(20, 40, 140), (240, 240, 236)]   # recoloured after the atlas's own base
TOL_KEYS = ('c', 'fc', 'dark', 'light', 'fl')


# ----------------------------------------------------------------------------
# Literals
def f32(v) -> float:
    """The float32 value of the literal the emitter prints for v."""
    return float(np.float32(float('%.5f' % float(v))))


def lit(v) -> str:
    return '%.5ff' % f32(v)


def resolve_params(P: dict, w: int, h: int) -> dict:
    """Lab params with default tolerances merged into every ref, rects
    normalised by the stock (w, h), and every float replaced by the float32
    value its emitted literal carries."""
    def tol(base, ref):
        t = dict(base)
        t.update(ref.get('tol', {}))
        return {k: f32(t[k]) for k in TOL_KEYS}

    def rect(r):
        r = lab.norm_rect(r, w, h)
        if not (0.0 <= r[0] < r[2] <= 1.0 and 0.0 <= r[1] < r[3] <= 1.0):
            raise SystemExit('bad rect %s' % r)
        return [f32(v) for v in r]

    def ref(r, base):
        out = {'tol': tol(base, r)}
        if 'uv' in r:
            out['uv'] = rect(r['uv'])
        else:
            out['rgb'] = [int(c) for c in r['rgb']]
            if any(c < 0 or c > 255 for c in out['rgb']):
                raise SystemExit('bad rgb %s' % r['rgb'])
        return out

    default_tol = dict(lab.DEFAULT_TOL)
    default_tol.update(P.get('tol', {}))
    grow = P.get('grow')
    return {
        'tol': {k: f32(default_tol[k]) for k in TOL_KEYS},
        'refs': [ref(r, default_tol) for r in P.get('refs', [])],
        'exclude_refs': [ref(r, lab.DEFAULT_EXCL_TOL) for r in P.get('exclude_refs', [])],
        'include': [rect(r) for r in P.get('include', [])],
        'exclude': [rect(r) for r in P.get('exclude', [])],
        'force': [rect(r) for r in P.get('force', [])],
        'clean': int(P.get('clean', 0)),
        'grow': [int(grow[0]), f32(grow[1]), f32(grow[2])] if grow else None,
    }


def c_tol(t):
    return '{%s}' % ', '.join(lit(t[k]) for k in TOL_KEYS)


def c_rect(r):
    return '{%s}' % ', '.join(lit(v) for v in r)


def c_ref(r):
    if 'uv' in r:
        return '{PaintRef::Kind::Box, {0, 0, 0}, %s, %s}' % (c_rect(r['uv']), c_tol(r['tol']))
    return '{PaintRef::Kind::Srgb, {%d, %d, %d}, %s, %s}' % (
        *r['rgb'], c_rect([0.0, 0.0, 0.0, 0.0]), c_tol(r['tol']))


def c_list(ctype, name, items):
    """(declaration lines, 'pointer, count' initialiser)."""
    if not items:
        return [], 'nullptr, 0'
    lines = ['static constexpr %s %s[] = {' % (ctype, name)]
    lines += ['    %s,' % item for item in items]
    lines.append('};')
    return lines, '%s, static_cast<uint16_t>(std::size(%s))' % (name, name)


def c_mask_params(ident, R):
    lines, fields = [], []
    for suffix, ctype, items in (
            ('Refs', 'PaintRef', [c_ref(r) for r in R['refs']]),
            ('ExcludeRefs', 'PaintRef', [c_ref(r) for r in R['exclude_refs']]),
            ('Include', 'UvRect', [c_rect(r) for r in R['include']]),
            ('Exclude', 'UvRect', [c_rect(r) for r in R['exclude']]),
            ('Force', 'UvRect', [c_rect(r) for r in R['force']])):
        decl, init = c_list(ctype, 'k%s%s' % (ident, suffix), items)
        lines += decl
        fields.append(init)
    passes, gdark, glight = R['grow'] or [0, 0.0, 0.0]
    fields.append('%d, %d, %s, %s' % (R['clean'], passes, lit(gdark), lit(glight)))
    lines.append('static constexpr PaintMaskParams k%sParams = {' % ident)
    lines += ['    %s,' % f for f in fields]
    lines.append('};')
    return lines


def c_bytes(name, arr, per_line=64):
    flat = np.asarray(arr, dtype=np.uint8).reshape(-1)
    lines = ['static constexpr uint8_t %s[] = {' % name]
    for i in range(0, len(flat), per_line):
        lines.append('    %s,' % ', '.join(str(int(v)) for v in flat[i:i + per_line]))
    lines.append('};')
    return lines


def c_colour(c):
    return '{%d, %d, %d}' % tuple(int(v) for v in c)


# ----------------------------------------------------------------------------
# Synthetic atlases. Formulas only, no random numbers: regenerating must
# reproduce the file byte for byte.
def red_atlas():
    """Coloured paint that exercises every gate feature (see the params)."""
    img = np.zeros((16, 16, 4), np.uint8)
    img[..., :3] = (40, 40, 42)
    img[..., 3] = 255
    paint = np.array([120, 44, 24], float)
    for y in range(2, 12):                       # the panel, with shading
        for x in range(1, 13):
            f = 0.72 + 0.06 * ((x + 2 * y) % 6)
            img[y, x, :3] = np.clip(np.round(paint * f), 0, 255)
    img[3, 2:6, :3] = (160, 67, 55)              # highlight matched by the srgb ref
    img[4:6, 9:11, :3] = (190, 72, 44)           # lamp: paint hue, removed by the exclude ref
    img[6, 6, :3] = (92, 74, 60)                 # rust speck that the clean pass fills
    for x in range(1, 13):                       # hue drift: soft membership
        img[10, x, :3] = (120, 44 + 7 * (x - 1), 24)
    img[11, 1:5, :3] = (14, 6, 4)                # darker than the lightness floor
    img[11, 5:9, :3] = (30, 12, 8)
    img[2:12, 13, :3] = (100, 62, 48)            # seam: wrong hue, right lightness, grown
    img[0:2, 4:10, :3] = paint                   # paint-coloured glass
    img[0:2, 4:10, 3] = 150
    img[7, 11, 3] = 252                          # partial alpha
    img[12, 2:6, :3] = (150, 150, 155)           # chrome strip that force paints
    img[3, 14:16, :3] = paint                    # outside the include rect
    img[14, 1:4, :3] = paint
    img[13, 8, :3] = paint                       # isolated speck that the clean pass removes
    img[13:15, 10:13, :3] = (30, 60, 170)        # blue trim
    img[15, 8:16, :3] = (240, 240, 240)          # white
    params = {
        'refs': [{'uv': [6 / 16, 5 / 16, 10 / 16, 9 / 16]},
                 {'rgb': [160, 67, 55], 'tol': {'c': 0.03, 'fc': 0.02, 'dark': 0.10, 'light': 0.10, 'fl': 0.04}}],
        'tol': {'c': 0.08, 'fc': 0.05, 'dark': 0.30, 'light': 0.25, 'fl': 0.06},
        'exclude_refs': [{'rgb': [190, 72, 44]}],
        'include': [[0, 0, 14 / 16, 14 / 16]],
        'exclude': [[3 / 16, 8 / 16, 5 / 16, 10 / 16]],
        'force': [[2 / 16, 12 / 16, 6 / 16, 13 / 16]],
        'clean': 1,
        'grow': [1, 0.04, 0.20],
    }
    return img, params


def cream_atlas():
    """A low-chroma paint, so the neutral and hue-relative schemes blend."""
    img = np.zeros((16, 16, 4), np.uint8)
    img[..., :3] = (60, 62, 66)
    img[..., 3] = 255
    paint = np.array([210, 195, 160], float)
    for y in range(1, 15):
        for x in range(1, 15):
            f = 0.80 + 0.05 * ((3 * x + y) % 5)
            img[y, x, :3] = np.clip(np.round(paint * f), 0, 255)
    img[7, 1:15, :3] = (40, 60, 160)             # livery stripe the colour gate keeps
    img[3:5, 10:14, :3] = (225, 225, 230)        # chrome, rect-excluded
    img[12:14, 12:14, 3] = 148                   # glass
    params = {
        'refs': [{'uv': [2 / 16, 9 / 16, 8 / 16, 13 / 16]}],
        'tol': {'c': 0.05, 'fc': 0.04, 'dark': 0.20, 'light': 0.15, 'fl': 0.04},
        'exclude': [[10 / 16, 3 / 16, 14 / 16, 5 / 16]],
    }
    return img, params


def red_repaint_atlas():
    """An alternate livery of the red atlas: the same UVs and params, green
    paint, and green over what the red atlas calls trim, so the red atlas's
    mask has to bound it."""
    img, params = red_atlas()
    img[..., [0, 1]] = img[..., [1, 0]]
    img[13:15, 10:13, :3] = (44, 120, 24)
    return img, params


def faint_atlas():
    """Paint behind alpha 252: no texel reaches weight .5, so the base colour
    takes the m + 1e-9 fallback."""
    img = np.zeros((16, 16, 4), np.uint8)
    img[..., :3] = (70, 70, 74)
    img[..., 3] = 255
    paint = np.array([60, 110, 170], float)
    for y in range(3, 13):
        for x in range(3, 13):
            f = 0.85 + 0.05 * ((x + y) % 4)
            img[y, x, :3] = np.clip(np.round(paint * f), 0, 255)
    img[3:13, 3:13, 3] = 252
    params = {
        'refs': [{'rgb': [60, 110, 170]}],
        'tol': {'c': 0.05, 'fc': 0.04, 'dark': 0.20, 'light': 0.20, 'fl': 0.05},
    }
    return img, params


def steel_atlas():
    """A low-chroma paint with off-hue texels forced into the mask. The
    hue-relative carry divides by the base's small chroma, so recolouring to
    the 8-bit base colour is a no-op only because a pick within 0.008 of the
    base snaps to it."""
    img = np.zeros((16, 16, 4), np.uint8)
    img[..., :3] = (40, 40, 40)
    img[..., 3] = 255
    paint = np.array([90, 120, 160], float)
    for y in range(1, 15):
        for x in range(1, 15):
            f = 0.85 + 0.05 * ((2 * x + y) % 5)
            img[y, x, :3] = np.clip(np.round(paint * f), 0, 255)
    img[7, 2:8, :3] = (200, 40, 40)
    img[7, 8:14, :3] = (220, 200, 30)
    img[12:14, 3:5, 3] = 150                     # glass
    params = {
        'refs': [{'rgb': [90, 120, 160]}],
        'tol': {'c': 0.05, 'fc': 0.04, 'dark': 0.25, 'light': 0.25, 'fl': 0.05},
        'force': [[2 / 16, 7 / 16, 14 / 16, 8 / 16]],
    }
    return img, params


# (ident, name, make, region). A case with a region is an alternate atlas: it is
# gated with its own pixels and bounded by the named earlier case's lab mask,
# exactly as paint_lab.derive bounds an alternate by its stock atlas.
GOLDEN_CASES = [
    ('Red', 'red', red_atlas, None),
    ('RedRepaint', 'red_repaint', red_repaint_atlas, 'Red'),
    ('Cream', 'cream', cream_atlas, None),
    ('Faint', 'faint', faint_atlas, None),
    ('Steel', 'steel', steel_atlas, None),
]


def golden_case(ident, name, rgba, P, region=None):
    """region: None, or (ident, lab mask) of the case bounding this one.
    Returns (lines, mask)."""
    h, w = rgba.shape[:2]
    R = resolve_params(P, w, h)
    mask, trust = lab.gate(rgba, R)
    if region is not None:
        mask = np.minimum(mask, region[1])
    base = lab.base_colour(rgba, mask)
    base8 = tuple(int(v) for v in lab.oklab_to_srgb8(base))
    targets = [base8] + GOLDEN_TARGETS
    recolours = [lab.recolour(rgba, mask, base, t, trust) for t in targets]
    q8 = lambda m: np.clip(np.round(m * 255.0), 0, 255).astype(np.uint8)  # noqa: E731
    painted = int((mask >= 0.5).sum())
    weight_sum = int(q8(mask).astype(np.int64).sum())

    lines = ['// %s' % name]
    lines += c_mask_params(ident, R)

    def both(label, arr):
        lines.extend(c_bytes('k%s%sTopDown' % (ident, label), arr))
        lines.extend(c_bytes('k%s%sBottomUp' % (ident, label), arr[::-1]))
        return '{k%s%sTopDown, k%s%sBottomUp}' % (ident, label, ident, label)

    rgba_ref = both('Rgba', rgba)
    mask_ref = both('MaskQ8', q8(mask))
    trust_ref = both('TrustQ8', q8(trust))
    recolour_refs = [both('Recolour%d' % i, out) for i, out in enumerate(recolours)]
    lines += [
        'static constexpr PaintGoldenCase k%sCase = {' % ident,
        '    "%s", %d, %d, &k%sParams, %s,' % (
            name, w, h, ident, '&k%sCase' % region[0] if region is not None else 'nullptr'),
        '    %s,' % rgba_ref,
        '    %s,' % mask_ref,
        '    %s,' % trust_ref,
        '    %s, %d, %d,' % (c_colour(base8), painted, weight_sum),
        '    {%s},' % ', '.join(c_colour(t) for t in targets),
        '    {%s},' % ', '.join(recolour_refs),
        '};',
        '',
    ]
    return lines, mask


def golden_text():
    lines = [
        '// GENERATED by tools/paint_profiles.py golden -- do not edit.',
        '// Regenerate: python3 tools/paint_profiles.py golden',
        '// The float64 output of tools/paint_lab.py on synthetic 16x16 atlases.',
        '',
    ]
    masks = {}
    for ident, name, make, region in GOLDEN_CASES:
        rgba, params = make()
        case_lines, masks[ident] = golden_case(
            ident, name, rgba, params, (region, masks[region]) if region is not None else None)
        lines += case_lines
    lines.append('static constexpr const PaintGoldenCase* kPaintGoldenCases[] = {%s};' % ', '.join(
        '&k%sCase' % ident for ident, _, _, _ in GOLDEN_CASES))
    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    g = sub.add_parser('golden')
    g.add_argument('--check', action='store_true', help='exit 1 if the committed file is stale')
    a = ap.parse_args()
    if a.cmd == 'golden':
        text = golden_text()
        if a.check:
            if not GOLDEN.exists() or GOLDEN.read_text() != text:
                raise SystemExit('%s is stale: run tools/paint_profiles.py golden' % GOLDEN.relative_to(REPO))
            print('%s is current' % GOLDEN.relative_to(REPO))
            return
        GOLDEN.write_text(text)
        print('wrote %s' % GOLDEN.relative_to(REPO))


if __name__ == '__main__':
    main()
