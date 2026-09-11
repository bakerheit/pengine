#!/usr/bin/env python3
"""Cook Car 8's ambulance paint from its stock atlas plus two generated panels.

Car 8 is an import with no body generator, so its atlas is the only authority
for where anything sits. This cook never redraws the van: it re-paints it.

Two flat panels were generated as *edits of the stock atlas crops*, so they
carry the livery and nothing else -- every rib, seam, window, lamp and reflector
came back where it started. The cook folds each one in as a ratio against the
crop it was generated from:

    out = base * (generated / source)

Where the edit changed nothing the ratio is 1 and the stock pixel survives
untouched, so a 16x downsample of painted artwork cannot smear the one-pixel
panel lines a 128x128 atlas is built out of. Only where paint was actually laid
down does the ratio carry it.

The panels cover the flanks and the rear doors. The roof, nose, hood and cab
front are left for the tone curve at the bottom of this file, which lifts the
stock grey to the same white the generated panels settled on -- measured off
those panels rather than picked by eye, so the whole shell reads as one colour.

The flank island is shared mirrored by both sides of the van, which is why
"AMBULANCE" reads correctly on one flank and backwards on the other. That is
the atlas's decision, not this cook's; mail.png's envelope does the same thing.

Run from the repository root:

    python3 tools/make_car8_ambulance_texture.py
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
TEXTURES = ROOT / "assets/textures/vehicles/car8"

# Atlas pixel rectangles, (left, top, right, bottom), read off body.emesh's UV
# islands rather than eyeballed. Regenerate with tools/dump_car8_uv_islands.py
# if the body cook is ever replaced.
FLANK = (6, 75, 126, 126)  # both flanks, mirrored onto one island
REAR = (101, 3, 126, 54)  # rear cargo doors

# Stock grey -> ambulance white, sampled from the unpainted areas of the
# generated flank panel. Below ~60 the generated panel left glass and rubber
# alone, so the curve is deliberately flat down there.
TONE = (
    (0, 0),
    (40, 42),
    (60, 60),
    (90, 100),
    (120, 175),
    (140, 193),
    (172, 232),
    (190, 243),
    (215, 250),
    (240, 254),
    (255, 255),
)


def ratio_transfer(base: Image.Image, generated: Image.Image) -> Image.Image:
    """Apply a generated panel's edit to the crisp atlas crop it came from."""
    w, h = base.size
    source = base.resize((w * 8, h * 8), Image.NEAREST)
    src = np.asarray(source.resize((w, h), Image.LANCZOS), dtype=np.float64)
    gen = np.asarray(generated.resize((w, h), Image.LANCZOS), dtype=np.float64)
    crop = np.asarray(base, dtype=np.float64)

    # The epsilon keeps near-black glass from turning a rounding error into a
    # large ratio; the clamp keeps a stray bright pixel from blowing out.
    eps = 12.0
    out = crop * np.clip((gen + eps) / (src + eps), 0.0, 6.0)

    # Over glass and tyre-black a ratio says nothing useful, so paint laid on
    # top of them is carried additively instead.
    dark = (src.mean(axis=2) < 40.0)[..., None]
    out = np.where(dark, crop + (gen - src), out)
    return Image.fromarray(np.clip(out, 0, 255).round().astype(np.uint8))


def tone_curve() -> np.ndarray:
    xs = np.array([p[0] for p in TONE], dtype=np.float64)
    ys = np.array([p[1] for p in TONE], dtype=np.float64)
    return np.interp(np.arange(256, dtype=np.float64), xs, ys)


def body_mask(mesh: Path) -> np.ndarray:
    """Pixels the body mesh actually samples.

    The atlas also carries the shared wheel's tyre and hub in its top-left
    corner. Those are not body paint and must not be whitened, and the mesh is
    the only honest way to tell the difference.
    """
    from PIL import ImageDraw

    from bake_vehicle_surfaces import read_mesh

    vertices, indices = read_mesh(mesh)
    scale = 8
    size = 128 * scale
    canvas = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(canvas)
    for triangle in indices:
        uv = vertices[triangle, 6:8]
        draw.polygon(
            [(float(u) * size, (1.0 - float(v)) * size) for u, v in uv], fill=255
        )
    return np.asarray(canvas.resize((128, 128), Image.BOX)) > 8


def cook(base_path: Path, flank_art: Path, rear_art: Path, mesh: Path,
         out_path: Path) -> None:
    base = Image.open(base_path).convert("RGB")
    if base.size != (128, 128):
        raise SystemExit(f"{base_path} is {base.size}, expected 128x128")
    out = base.copy()

    for rect, art in ((FLANK, flank_art), (REAR, rear_art)):
        with Image.open(art) as image:
            panel = ratio_transfer(base.crop(rect), image.convert("RGB"))
        out.paste(panel, rect[:2])

    # Everything the two panels did not cover -- roof, nose, hood, cab front --
    # is lifted onto the same white so the shell does not read two-tone.
    painted = np.zeros((128, 128), dtype=bool)
    for left, top, right, bottom in (FLANK, REAR):
        painted[top:bottom, left:right] = True
    lift = body_mask(mesh) & ~painted

    pixels = np.asarray(out, dtype=np.uint8).copy()
    curve = tone_curve()
    pixels[lift] = np.clip(curve[pixels[lift]], 0, 255).round().astype(np.uint8)
    Image.fromarray(pixels).save(out_path)
    print(f"wrote {out_path.relative_to(ROOT)}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, default=TEXTURES / "body.png")
    parser.add_argument(
        "--flank", type=Path, default=TEXTURES / "ambulance-flank-reference.png"
    )
    parser.add_argument(
        "--rear", type=Path, default=TEXTURES / "ambulance-rear-reference.png"
    )
    parser.add_argument(
        "--mesh", type=Path,
        default=ROOT / "assets/models/vehicles/car8/body.emesh",
    )
    parser.add_argument("--out", type=Path, default=TEXTURES / "ambulance.png")
    args = parser.parse_args()
    cook(args.base, args.flank, args.rear, args.mesh, args.out)


if __name__ == "__main__":
    import sys

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    main()
