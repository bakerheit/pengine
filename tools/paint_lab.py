#!/usr/bin/env python3
"""Paint lab: the reference paint mask and shading-preserving recolour for
vehicle atlases, plus the tooling a paint profile is tuned with.

src/game/vehicle_paint.{h,cpp} is a port of `gate`, `base_colour` and
`recolour` below. tests/vehicle_paint_golden.inc is this file's output on
synthetic atlases (`tools/paint_profiles.py golden`), and vehicle_paint_tests
holds the port to it within one 8-bit level. Change the maths here and in the
port in the same commit, then regenerate the golden.

Pipeline for one atlas X worn by a model with params P:
    (stock_mask, _) = gate(stock atlas, P)          # the paint REGION
    (gx, trust)     = gate(X, P)                    # X's own paint + colour trust
    mask(X)         = min(stock_mask, gx) on an alternate, else stock_mask
    base(X)         = weighted per-channel median OKLab of X under mask(X)
    out(X, T)       = mix(X, recolour(X, base(X), T, trust), mask(X))   # alpha untouched

Coordinates: every rect is normalised IMAGE space of the PNG as authored,
(0,0) = top-left corner, (1,1) = bottom-right. Texel (x,y) is inside
[u0,v0,u1,v1] when its centre cu=(x+.5)/W, cv=(y+.5)/H has u0<=cu<u1 and
v0<=cv<v1. The engine decodes with rows flipped, so the port tests the image
row the buffer row came from. Params JSON may give rects in authoring pixels
[x0,y0,x1,y1) of the stock atlas (any value > 1).

Params JSON:
  slug, stock        atlas path relative to assets/textures/vehicles/
  alternates         same size and UVs as stock, same params, stock mask as region
  coverage           [[mesh_slug, [emesh stems]]]; sheet overlay only
  refs               [{"rgb":[r,g,b]} | {"uv":[u0,v0,u1,v1]}, optional "tol"]
                     a uv box is sampled from the atlas being gated (median OKLab)
  tol                {c, fc, dark, light, fl}; defaults below
  exclude_refs       same shape; default tol DEFAULT_EXCL_TOL
  include / exclude / force   rect lists; force paints, include and exclude win
  clean              3x3 median passes
  grow               null or [passes, dark, light]
  region_from_stock  default true
  overrides          {"<atlas file name>": {extra refs/rects/clean/grow}}

Commands:
  paint_lab.py sheet PARAMS.json [--only STEM] [--scale N]   contact sheet per atlas
  paint_lab.py all                                           every tools/paint_profiles/*.json
  paint_lab.py probe ATLAS [--rect x0 y0 x1 y1] [--top N]    dominant colours
  paint_lab.py pick ATLAS x y [x1 y1]                        texel / box median
  paint_lab.py zoom PARAMS.json --rect x0 y0 x1 y1 [--atlas REL] [--scale N]
                    [--targets r,g,b ...]                    close-up of one region
ATLAS is relative to assets/textures/vehicles/. Images go to build/paint_lab/.
Needs numpy and Pillow. It is a tuning tool and is not part of tools/ci.sh.
"""
from __future__ import annotations

import argparse
import json
import struct
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[1]
TEX = REPO / 'assets/textures/vehicles'
MODELS = REPO / 'assets/models/vehicles'
OUT = REPO / 'build/paint_lab'
PARAMS_DIR = REPO / 'tools/paint_profiles'

# ----------------------------------------------------------------------------
# Global constants (constexpr in src/game/vehicle_paint.h, not per model).
L_FLOOR = 0.10                    # lightness floor when normalising chroma by L
SAT_W_LO, SAT_W_HI = 0.04, 0.12   # |q_base| blend: neutral scheme -> hue-relative
HUE_KEEP_FAR = 0.35               # fraction of a texel's hue offset kept on a far target
TARGET_NEAR = 0.03                # below this |q_target - q_base| the target IS the base
TARGET_FAR = 0.25                 # |q_target - q_base| at which a target counts as "far"
UNTRUSTED_L_KEEP = 0.5            # lightness offset kept by a texel whose colour failed the refs
TARGET_SNAP = 0.008               # OKLab distance under which a picked 8-bit colour IS the base
ALPHA_LO, ALPHA_HI = 250.0, 255.0 # glass-alpha texels are never paint
GAMUT_ITERS = 14
HIST_BINS = 1024
DEFAULT_TOL = dict(c=0.06, fc=0.05, dark=0.35, light=0.20, fl=0.08)
DEFAULT_EXCL_TOL = dict(c=0.04, fc=0.03, dark=0.06, light=0.06, fl=0.04)

TARGETS = [
    ('deep blue', (20, 40, 140)),
    ('white', (240, 240, 236)),
    ('black', (18, 18, 20)),
    ('lime', (120, 210, 40)),
]

# ----------------------------------------------------------------------------
# sRGB8 <-> OKLab (Ottosson 2020), float64.
M1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929],
               [0.2119034982, 0.6806995451, 0.1073969566],
               [0.0883024619, 0.2817188376, 0.6299787005]])
M2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468],
               [1.9779984951, -2.4285922050, 0.4505937099],
               [0.0259040371, 0.7827717662, -0.8086757660]])
M2I = np.array([[1.0, 0.3963377774, 0.2158037573],
                [1.0, -0.1055613458, -0.0638541728],
                [1.0, -0.0894841775, -1.2914855480]])
M1I = np.array([[4.0767416621, -3.3077115913, 0.2309699292],
                [-1.2684380046, 2.6097574011, -0.3413193965],
                [-0.0041960863, -0.7034186147, 1.7076147010]])


def _mul(x, M):
    return np.einsum('...j,ij->...i', x, M)


def srgb8_to_linear(c8):
    c = np.asarray(c8, dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb8(lin):
    c = np.clip(lin, 0.0, 1.0)
    s = np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)
    return np.clip(np.round(s * 255.0), 0, 255).astype(np.uint8)


def linear_to_oklab(lin):
    return _mul(np.cbrt(_mul(lin, M1)), M2)


def oklab_to_linear(lab):
    return _mul(_mul(np.asarray(lab, dtype=np.float64), M2I) ** 3, M1I)


def srgb8_to_oklab(rgb8):
    return linear_to_oklab(srgb8_to_linear(rgb8))


def oklab_to_srgb8(lab):
    return linear_to_srgb8(oklab_to_linear(lab))


def smoothstep(e0, e1, x):
    t = np.clip((np.asarray(x, dtype=np.float64) - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# ----------------------------------------------------------------------------
# Params
def load_params(path) -> dict:
    p = json.loads(Path(path).read_text())
    for key, default in (('refs', []), ('exclude_refs', []), ('include', []),
                         ('exclude', []), ('force', []), ('clean', 0), ('grow', None),
                         ('alternates', []), ('region_from_stock', True),
                         ('overrides', {}), ('coverage', [])):
        p.setdefault(key, default)
    tol = dict(DEFAULT_TOL)
    tol.update(p.get('tol', {}))
    p['tol'] = tol
    return p


def load_rgba(rel: str) -> np.ndarray:
    return np.array(Image.open(TEX / rel).convert('RGBA'))


def norm_rect(r, w, h):
    r = [float(x) for x in r]
    if max(r) > 1.0:
        return [r[0] / w, r[1] / h, r[2] / w, r[3] / h]
    return r


def rect_mask(rects, w, h, px_w=None, px_h=None) -> np.ndarray:
    """px_w/px_h: size of the atlas pixel rects were authored against (the stock)."""
    pw, ph = px_w or w, px_h or h
    cu = (np.arange(w) + 0.5) / w
    cv = (np.arange(h) + 0.5) / h
    m = np.zeros((h, w), bool)
    for r in rects:
        u0, v0, u1, v1 = norm_rect(r, pw, ph)
        m |= ((cv[:, None] >= v0) & (cv[:, None] < v1)) & ((cu[None, :] >= u0) & (cu[None, :] < u1))
    return m


def ref_colour(ref: dict, rgba: np.ndarray) -> np.ndarray:
    """OKLab of a reference: explicit sRGB, or per-channel median OKLab of a box in THIS atlas."""
    if 'rgb' in ref:
        return srgb8_to_oklab(np.array(ref['rgb'], dtype=np.float64))
    h, w = rgba.shape[:2]
    sel = rect_mask([ref['uv']], w, h)
    return np.median(srgb8_to_oklab(rgba[..., :3][sel]), axis=0)


def lightness_window(L, ref_L, tol):
    dL = L - ref_L
    return np.where(dL < 0,
                    1.0 - smoothstep(tol['dark'], tol['dark'] + tol['fl'], -dL),
                    1.0 - smoothstep(tol['light'], tol['light'] + tol['fl'], dL))


def gate_one(lab, ref_lab, tol):
    """Soft membership of every texel in one reference paint.

    q = (a,b)/max(L,L_FLOOR) is chroma per unit lightness, which a pure shading
    change (scale in linear light) leaves nearly unchanged, so a shadowed panel
    and a lit panel of the same paint land close together in q.
    """
    L = lab[..., 0]
    q = lab[..., 1:] / np.maximum(L, L_FLOOR)[..., None]
    qr = ref_lab[1:] / max(ref_lab[0], L_FLOOR)
    dq = np.linalg.norm(q - qr, axis=-1)
    wc = 1.0 - smoothstep(tol['c'], tol['c'] + tol['fc'], dq)
    return wc * lightness_window(L, ref_lab[0], tol)


def median3(m):
    p = np.pad(m, 1, mode='edge')
    h, w = m.shape
    return np.median(np.stack([p[y:y + h, x:x + w] for y in range(3) for x in range(3)], 0), axis=0)


def max3(m):
    p = np.pad(m, 1, mode='edge')
    h, w = m.shape
    return np.max(np.stack([p[y:y + h, x:x + w] for y in range(3) for x in range(3)], 0), axis=0)


def gate(rgba, P, overrides=None, px_size=None):
    """Returns (mask, trust), both float 0..1 per texel.

    trust = how well the texel's own colour matched the refs (before rects,
    cleaning and growth). Texels that are paint only because of clean/grow/force
    have low trust; recolour gives them the target's chroma instead of carrying
    their own when the target is far from the base.
    """
    h, w = rgba.shape[:2]
    pw, ph = px_size or (w, h)
    lab = srgb8_to_oklab(rgba[..., :3])
    ov = overrides or {}
    refs = P['refs'] + ov.get('refs', [])
    colour = np.zeros((h, w))
    lwin = np.zeros((h, w))
    ref_labs = []
    chroma = np.zeros((h, w))
    q = lab[..., 1:] / np.maximum(lab[..., 0], L_FLOOR)[..., None]
    for ref in refs:
        tol = dict(P['tol']); tol.update(ref.get('tol', {}))
        rl = ref_colour(ref, rgba)
        ref_labs.append((rl, tol))
        colour = np.maximum(colour, gate_one(lab, rl, tol))
        # trust measures the CHROMA match only: a shaded texel of the right hue
        # that sits at the edge of the lightness window is still trusted paint.
        dq = np.linalg.norm(q - rl[1:] / max(rl[0], L_FLOOR), axis=-1)
        chroma = np.maximum(chroma, 1.0 - smoothstep(tol['c'], tol['c'] + tol['fc'], dq))
    ex = np.zeros((h, w))
    for ref in P['exclude_refs'] + ov.get('exclude_refs', []):
        tol = dict(DEFAULT_EXCL_TOL); tol.update(ref.get('tol', {}))
        ex = np.maximum(ex, gate_one(lab, ref_colour(ref, rgba), tol))
    colour = colour * (1.0 - ex)
    trust = chroma * (1.0 - ex)

    inc = P['include'] + ov.get('include', [])
    inc_m = rect_mask(inc, w, h, pw, ph) if inc else None
    exc = P['exclude'] + ov.get('exclude', [])
    exc_m = rect_mask(exc, w, h, pw, ph) if exc else None

    def rects(m):
        if inc_m is not None:
            m = m * inc_m
        if exc_m is not None:
            m = m * ~exc_m
        return m

    m = rects(colour)
    for _ in range(int(ov.get('clean', P['clean']))):
        m = rects(median3(m))
    grow = ov.get('grow', P['grow'])
    if grow:
        passes, gdark, glight = int(grow[0]), float(grow[1]), float(grow[2])
        L = lab[..., 0]
        for rl, tol in ref_labs:
            lwin = np.maximum(lwin, lightness_window(L, rl[0], dict(tol, dark=gdark, light=glight)))
        lwin = lwin * (1.0 - ex)
        for _ in range(passes):
            m = rects(np.maximum(m, max3(m) * lwin))
    force = P['force'] + ov.get('force', [])
    if force:
        m = rects(np.maximum(m, rect_mask(force, w, h, pw, ph).astype(float)))
    a = rgba[..., 3].astype(np.float64)
    m = m * np.clip((a - ALPHA_LO) / (ALPHA_HI - ALPHA_LO), 0.0, 1.0)
    return np.clip(m, 0.0, 1.0), np.clip(trust, 0.0, 1.0)


def derive(P, atlas_rel, stock_mask=None, stock_size=None):
    rgba = load_rgba(atlas_rel)
    g, trust = gate(rgba, P, P['overrides'].get(Path(atlas_rel).name), stock_size)
    if atlas_rel != P['stock'] and P['region_from_stock'] and stock_mask is not None \
            and stock_mask.shape == g.shape:
        g = np.minimum(g, stock_mask)
    return g, trust


# ----------------------------------------------------------------------------
def hist_median(values, weights, lo, hi):
    idx = np.clip(((values - lo) / (hi - lo) * HIST_BINS).astype(int), 0, HIST_BINS - 1)
    c = np.cumsum(np.bincount(idx.ravel(), weights=weights.ravel(), minlength=HIST_BINS))
    k = int(np.searchsorted(c, c[-1] * 0.5))
    return lo + (k + 0.5) * (hi - lo) / HIST_BINS


def base_colour(rgba, mask):
    lab = srgb8_to_oklab(rgba[..., :3])
    wgt = np.where(mask >= 0.5, mask, 0.0)
    if wgt.sum() <= 0:
        wgt = mask + 1e-9
    return np.array([hist_median(lab[..., 0], wgt, 0.0, 1.0),
                     hist_median(lab[..., 1], wgt, -0.5, 0.5),
                     hist_median(lab[..., 2], wgt, -0.5, 0.5)])


# ----------------------------------------------------------------------------
def remap_lightness(L, Lr, Lt):
    """Identity when Lt == Lr; additive contrast near the base; never clips.

    Above the base: x = (L-Lr)/Hs with Hs = 1-Lr; F(x) = r x / (1 + (r-1) x),
    r = Hs/Ht, Ht = 1-Lt; L' = Lt + Ht F(x). Below the base the same curve maps
    the toe: y = (Lr-L)/Lr, r = Lr/Lt, L' = Lt - Lt F(y). F(0)=0, F(1)=1 and
    F'(0)=r, so a small step dL around the base keeps its size in the output,
    source white stays white and source black stays black.
    """
    eps = 1e-4
    dL = L - Lr
    Hs, Ht = max(1.0 - Lr, eps), max(1.0 - Lt, eps)
    Ss, St = max(Lr, eps), max(Lt, eps)
    x = np.clip(dL / Hs, 0.0, 1.0)
    r = Hs / Ht
    up = Lt + Ht * (r * x / (1.0 + (r - 1.0) * x))
    y = np.clip(-dL / Ss, 0.0, 1.0)
    r2 = Ss / St
    down = Lt - St * (r2 * y / (1.0 + (r2 - 1.0) * y))
    return np.where(dL >= 0, up, down)


def perp(v):
    return np.stack([-v[..., 1], v[..., 0]], -1)


def recolour_lab(lab, base, target, trust=None):
    # A pick within 8-bit rounding of the base is "the same paint": use the base
    # exactly, otherwise the rounding error is amplified on far-hue texels.
    if float(np.linalg.norm(np.asarray(target) - base)) < TARGET_SNAP:
        target = base
    L = lab[..., 0]
    q = lab[..., 1:] / np.maximum(L, L_FLOOR)[..., None]
    qr = base[1:] / max(base[0], L_FLOOR)
    qt = target[1:] / max(target[0], L_FLOOR)
    cr, ct = float(np.linalg.norm(qr)), float(np.linalg.norm(qt))
    # 0 when the target is the base (within 8-bit rounding of a picked colour).
    far = float(smoothstep(TARGET_NEAR, TARGET_FAR, float(np.linalg.norm(qt - qr))))
    t = np.ones(L.shape) if trust is None else 1.0 - far * (1.0 - trust)       # per-texel carry weight
    # Lightness: untrusted texels keep only part of their offset from the base.
    L_src = base[0] + (L - base[0]) * (UNTRUSTED_L_KEEP + (1.0 - UNTRUSTED_L_KEEP) * t)
    Lout = remap_lightness(L_src, base[0], target[0])
    # Neutral base: carry the chroma offset from the base.
    q_neu = qt + (q - qr) * t[..., None]
    if cr > 1e-6:
        # Coloured base: carry saturation ratio (along) and hue offset (across).
        # Unclamped: a clamp would break identity for far-hue texels.
        ur = qr / cr
        along = _mul(q, ur[None, :])[..., 0] / cr
        across = _mul(q, perp(ur)[None, :])[..., 0] / cr
        along = 1.0 + (along - 1.0) * t
        # Pixel-art paint hue-shifts highlights/shadows; rotating that offset
        # onto a far target reads as a wrong colour, so damp it with distance.
        across = across * t * (1.0 - (1.0 - HUE_KEEP_FAR) * far)
        ut = qt / ct if ct > 1e-6 else ur
        q_sat = qt * along[..., None] + perp(ut) * (across * ct)[..., None]
    else:
        q_sat = q_neu
    wsat = float(smoothstep(SAT_W_LO, SAT_W_HI, cr))
    qout = wsat * q_sat + (1.0 - wsat) * q_neu
    ab = qout * np.maximum(Lout, L_FLOOR)[..., None]
    return gamut_clip(np.concatenate([Lout[..., None], ab], -1))


def gamut_clip(lab):
    lin = oklab_to_linear(lab)
    bad = np.any((lin < -1e-6) | (lin > 1 + 1e-6), axis=-1)
    if not bad.any():
        return lab
    sub = lab[bad]
    lo, hi = np.zeros(len(sub)), np.ones(len(sub))
    for _ in range(GAMUT_ITERS):
        mid = (lo + hi) * 0.5
        lt = oklab_to_linear(np.concatenate([sub[:, :1], sub[:, 1:] * mid[:, None]], -1))
        ok = np.all((lt >= -1e-6) & (lt <= 1 + 1e-6), axis=-1)
        lo, hi = np.where(ok, mid, lo), np.where(ok, hi, mid)
    lab = lab.copy()
    lab[bad] = np.concatenate([sub[:, :1], sub[:, 1:] * lo[:, None]], -1)
    return lab


def recolour(rgba, mask, base, target_rgb8, trust=None):
    tgt = srgb8_to_oklab(np.array(target_rgb8, dtype=np.float64))
    rec = oklab_to_srgb8(recolour_lab(srgb8_to_oklab(rgba[..., :3]), base, tgt, trust)).astype(np.float64)
    src = rgba[..., :3].astype(np.float64)
    out = rgba.copy()
    out[..., :3] = np.clip(np.round(src + (rec - src) * mask[..., None]), 0, 255).astype(np.uint8)
    assert np.array_equal(out[..., 3], rgba[..., 3]), 'alpha changed'
    return out


# ----------------------------------------------------------------------------
# UV coverage: which texels a mesh can ever show, so a leak on an unused texel
# is not held against the mask. Reads cooked .emesh v2 (assets/models is not
# committed; copy it from a checkout that has it).
GLASS = {'windshield', 'rear_glass', 'passenger_glass', 'driver_glass',
         'driver_rear_glass', 'passenger_rear_glass'}


def read_emesh(path: Path):
    data = path.read_bytes()
    magic, version, _flags, nv, ni, _ns, _, _ = struct.unpack_from('<8I', data)
    assert magic == 0x48534D45 and version == 2, path
    v = np.frombuffer(data, dtype='<f4', count=nv * 12, offset=32).reshape(-1, 12)
    ids = np.frombuffer(data, dtype='<u4', count=ni, offset=32 + nv * 48).reshape(-1, 3)
    return v, ids


def mesh_coverage(slug: str, w: int, h: int, only=None) -> np.ndarray:
    """PNG row = (1 - v) * H, matching the row flip the engine decodes with."""
    cov = np.zeros((h, w), bool)
    for f in sorted((MODELS / slug).glob('*.emesh')):
        if f.stem in GLASS or (only and f.stem not in only):
            continue
        v, ids = read_emesh(f)
        uv = v[:, 6:8].astype(np.float64)
        for tri in ids:
            p = uv[tri]
            u = p[:, 0] * w
            vv = (1.0 - p[:, 1]) * h
            x0 = int(np.floor(max(0, u.min()))); x1 = int(np.ceil(min(w, u.max())))
            y0 = int(np.floor(max(0, vv.min()))); y1 = int(np.ceil(min(h, vv.max())))
            if x1 <= x0 or y1 <= y0:
                xi = int(np.clip(np.floor(u.mean()), 0, w - 1)); yi = int(np.clip(np.floor(vv.mean()), 0, h - 1))
                cov[yi, xi] = True
                continue
            ys, xs = np.mgrid[y0:y1, x0:x1]
            cx = xs + 0.5; cy = ys + 0.5
            (ax, bx, cx_), (ay, by, cy_) = u, vv
            d = (by - cy_) * (ax - cx_) + (cx_ - bx) * (ay - cy_)
            if abs(d) < 1e-12:
                continue
            l1 = ((by - cy_) * (cx - cx_) + (cx_ - bx) * (cy - cy_)) / d
            l2 = ((cy_ - ay) * (cx - cx_) + (ax - cx_) * (cy - cy_)) / d
            l3 = 1 - l1 - l2
            inside = (l1 >= -0.02) & (l2 >= -0.02) & (l3 >= -0.02)
            cov[ys[inside], xs[inside]] = True
            for k in range(3):   # thin slivers: the texels the vertices land in
                xi = int(np.clip(np.floor(u[k]), 0, w - 1)); yi = int(np.clip(np.floor(vv[k]), 0, h - 1))
                cov[yi, xi] = True
    return cov


def coverage_union(P, w, h):
    if not P['coverage']:
        return None
    cov = np.zeros((h, w), bool)
    for slug, stems in P['coverage']:
        cov |= mesh_coverage(slug, w, h, only=set(stems) or None)
    return cov


# ----------------------------------------------------------------------------
# Tooling
def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def to_tile(rgb, scale):
    im = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))
    return im.resize((im.width * scale, im.height * scale), Image.NEAREST)


def over_bg(rgba):
    a = rgba[..., 3:4].astype(float) / 255.0
    return rgba[..., :3] * a + np.array([255, 0, 255], float) * (1 - a)


def masks_for(P, atlas):
    """(rgba, mask, trust) for one atlas of a profile, stock region applied."""
    stock_rgba = load_rgba(P['stock'])
    sh, sw = stock_rgba.shape[:2]
    stock_mask, stock_trust = derive(P, P['stock'], None, (sw, sh))
    if atlas == P['stock']:
        return stock_rgba, stock_mask, stock_trust
    mask, trust = derive(P, atlas, stock_mask, (sw, sh))
    return load_rgba(atlas), mask, trust


def sheet(params_path, scale=None, only=None):
    P = load_params(params_path)
    slug = P['slug']
    OUT.mkdir(parents=True, exist_ok=True)
    results = []
    for atlas in [P['stock']] + P['alternates']:
        if only and Path(atlas).stem != only:
            continue
        t1 = time.perf_counter()
        rgba, mask, trust = masks_for(P, atlas)
        h, w = rgba.shape[:2]
        base = base_colour(rgba, mask)
        base8 = tuple(int(x) for x in oklab_to_srgb8(base))
        t_mask = time.perf_counter() - t1
        cov = coverage_union(P, w, h)
        sc = scale or max(1, 512 // w)
        tiles = [('original %dx%d' % (w, h), to_tile(over_bg(rgba), sc))]
        mg = np.repeat((mask * 255)[..., None], 3, -1)
        tiles.append(('mask  %.1f%% of atlas' % (100 * mask.mean()), to_tile(mg, sc)))
        if cov is not None:
            mc = mg.copy()
            mc[~cov] = mc[~cov] * 0.35 + np.array([20, 40, 110])
            tiles.append(('mask on mesh-used texels (blue = unused)', to_tile(mc, sc)))
        else:
            tiles.append(('painted region', to_tile(rgba[..., :3] * mask[..., None] + 30 * (1 - mask[..., None]), sc)))
        ident = recolour(rgba, mask, base, base8, trust)
        diff = int(np.abs(ident[..., :3].astype(int) - rgba[..., :3].astype(int)).max())
        tiles.append(('identity -> %s  maxdiff %d' % (base8, diff), to_tile(over_bg(ident), sc)))
        t2 = time.perf_counter()
        for name, tgt in TARGETS:
            tiles.append(('%s %s' % (name, tgt), to_tile(over_bg(recolour(rgba, mask, base, tgt, trust)), sc)))
        t_rec = (time.perf_counter() - t2) / len(TARGETS)
        tw, th = tiles[0][1].size
        cols, lab_h = 4, 22
        rows = (len(tiles) + cols - 1) // cols
        img = Image.new('RGB', (cols * (tw + 6) + 6, rows * (th + lab_h + 6) + 30), (24, 24, 28))
        d = ImageDraw.Draw(img)
        d.text((8, 6), '%s   atlas %s   base %s' % (slug, atlas, base8), fill=(255, 255, 255), font=font(18))
        for i, (label, t) in enumerate(tiles):
            x = 6 + (i % cols) * (tw + 6)
            y = 30 + (i // cols) * (th + lab_h + 6)
            d.text((x, y), label, fill=(230, 230, 230), font=font(15))
            img.paste(t, (x, y + lab_h))
        name = '%s__%s.png' % (slug, Path(atlas).stem)
        img.save(OUT / name)
        res = dict(slug=slug, atlas=atlas, size='%dx%d' % (w, h), painted_pct=round(100 * float(mask.mean()), 1),
                   used_painted_pct=(round(100 * float((mask * cov).sum() / max(1, cov.sum())), 1) if cov is not None else None),
                   base=base8, identity_maxdiff=diff, sheet=str(OUT / name),
                   py_mask_s=round(t_mask, 3), py_recolour_s=round(t_rec, 3))
        print(json.dumps(res))
        results.append(res)
    return results


def zoom(params_path, rect, atlas=None, scale=8, targets=('240,240,236', '20,40,140')):
    P = load_params(params_path)
    atlas = atlas or P['stock']
    rgba, mask, trust = masks_for(P, atlas)
    base = base_colour(rgba, mask)
    h, w = rgba.shape[:2]
    cov = coverage_union(P, w, h)
    x0, y0, x1, y1 = rect
    tiles = [to_tile(over_bg(rgba[y0:y1, x0:x1]), scale)]
    mg = np.repeat((mask * 255)[..., None], 3, -1)
    if cov is not None:
        mg[~cov] = mg[~cov] * 0.35 + np.array([20, 40, 110])
    tiles.append(to_tile(mg[y0:y1, x0:x1], scale))
    for t in targets:
        tgt = tuple(int(v) for v in t.split(','))
        tiles.append(to_tile(over_bg(recolour(rgba, mask, base, tgt, trust)[y0:y1, x0:x1]), scale))
    tw, th = tiles[0].size
    img = Image.new('RGB', (len(tiles) * (tw + 4), th + 20), (24, 24, 28))
    d = ImageDraw.Draw(img)
    d.text((4, 2), '%s %s rect %s' % (P['slug'], atlas, list(rect)), fill=(255, 255, 255), font=font(14))
    for i, t in enumerate(tiles):
        img.paste(t, (i * (tw + 4), 20))
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / ('%s__zoom_%d_%d_%d_%d.png' % (P['slug'], x0, y0, x1, y1))
    img.save(path)
    print(path)


def probe(atlas, rect=None, top=24):
    rgba = load_rgba(atlas)
    h, w = rgba.shape[:2]
    sel = np.ones((h, w), bool) if rect is None else rect_mask([rect], w, h)
    rgb = rgba[..., :3][sel].reshape(-1, 3)
    lab = srgb8_to_oklab(rgb)
    key = np.round(lab / np.array([0.03, 0.02, 0.02])).astype(int)
    _, inv, counts = np.unique(key, axis=0, return_inverse=True, return_counts=True)
    inv = inv.ravel()
    ys, xs = np.nonzero(sel)
    for k in np.argsort(-counts)[:top]:
        idx = inv == k
        med = tuple(int(v) for v in np.median(rgb[idx], axis=0))
        L, a, b = np.median(lab[idx], axis=0)
        print('%6d %5.1f%% rgb=%-16s L=%.3f a=%+.3f b=%+.3f |q|=%.3f  px x%d-%d y%d-%d' % (
            counts[k], 100 * counts[k] / len(rgb), med, L, a, b, np.hypot(a, b) / max(L, L_FLOOR),
            xs[idx].min(), xs[idx].max() + 1, ys[idx].min(), ys[idx].max() + 1))


def pick(atlas, x0, y0, x1=None, y1=None):
    rgba = load_rgba(atlas)
    if x1 is None:
        px = rgba[y0, x0]
        print('texel', (x0, y0), tuple(int(v) for v in px), 'oklab', np.round(srgb8_to_oklab(px[:3]), 4))
    else:
        box = rgba[y0:y1, x0:x1, :3].reshape(-1, 3)
        print('box median', tuple(int(v) for v in np.median(box, 0)), 'oklab', np.round(np.median(srgb8_to_oklab(box), 0), 4))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('sheet'); s.add_argument('params'); s.add_argument('--scale', type=int); s.add_argument('--only')
    sub.add_parser('all')
    pr = sub.add_parser('probe'); pr.add_argument('atlas'); pr.add_argument('--rect', type=float, nargs=4); pr.add_argument('--top', type=int, default=24)
    pk = sub.add_parser('pick'); pk.add_argument('atlas'); pk.add_argument('xy', type=int, nargs='+')
    z = sub.add_parser('zoom'); z.add_argument('params'); z.add_argument('--atlas')
    z.add_argument('--rect', type=int, nargs=4, required=True); z.add_argument('--scale', type=int, default=8)
    z.add_argument('--targets', nargs='*', default=['240,240,236', '20,40,140'])
    a = ap.parse_args()
    if a.cmd == 'sheet':
        sheet(a.params, a.scale, a.only)
    elif a.cmd == 'all':
        files = sorted(PARAMS_DIR.glob('*.json'))
        if not files:
            raise SystemExit('no params in %s' % PARAMS_DIR)
        res = []
        for p in files:
            res += sheet(p)
        (OUT / 'results.json').write_text(json.dumps(res, indent=1))
    elif a.cmd == 'probe':
        probe(a.atlas, a.rect, a.top)
    elif a.cmd == 'pick':
        pick(a.atlas, *a.xy)
    elif a.cmd == 'zoom':
        zoom(a.params, a.rect, a.atlas, a.scale, a.targets)


if __name__ == '__main__':
    main()
