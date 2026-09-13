#!/usr/bin/env python3
"""Cook the supplied fire spritesheet into the runtime flame atlas.

    python3 tools/cook_fire_sprites.py '/path/to/Fire Spritesheet.png'

Writes two tracked files:

    assets/textures/effects/fire_sheet.png   the atlas the renderer samples
    src/app/fire_sprite_sheet.h              its grid, as constants

The header exists so the cooker and the renderer cannot disagree about the
layout. A sprite atlas read with the wrong column count does not fail — it
plays a fire made of halves of two frames, which looks like a shader bug and is
not one.

THREE THINGS HAPPEN HERE AND EACH IS LOAD-BEARING.

**Empty cells are dropped.** A sheet is a rectangle and an animation is a
count; if the last row is short, the frames nobody drew are transparent, and a
runtime that plays all rows*columns frames blinks out once a loop. The cooker
counts cells that actually contain something and writes that number.

**RGB IS DILATED INTO THE TRANSPARENT TEXELS.** This is the one that looks like
a detail and is not. The supplied sheet stores black in every fully transparent
texel, which is correct and normal. Bilinear filtering and the mip chain both
average RGB *without* regard to alpha, so every flame edge samples a mix of
flame colour and black and the fire draws with a dark rim — worse the further
away it is, because the mips are where the averaging is worst. Flooding the
flame colour outward into the transparent region before scaling gives the
filter something sensible to average with. Alpha is untouched, so the shape is
exactly the shape that was drawn.

**Each cell is box-filtered down on its own grid.** 256 px per frame is more
than a fire ever covers on screen and eleven rows of it is 34 MB of VRAM for
one effect. At 128 the atlas is a quarter of that and still sharper than the
flame ever draws. Cells are power-of-two aligned, so the downsample never
straddles a cell boundary and no frame bleeds into its neighbour.
"""
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT_PNG = ROOT / 'assets/textures/effects/fire_sheet.png'
OUT_HEADER = ROOT / 'src/app/fire_sprite_sheet.h'

CELL = 256          # source cell size, in texels
TARGET = 128        # cooked cell size
DILATE_PASSES = 6   # how far the flame colour is flooded into the clear region


def dilate_rgb(rgb, alpha):
    """Flood RGB outward into fully transparent texels, alpha untouched.

    Six passes of "any texel with nothing in it takes the average of its
    populated neighbours". Six because the mip chain averages over blocks of up
    to 64 texels at the coarsest level this atlas reaches, and a two-texel skirt
    is gone by the third mip."""
    filled = alpha > 0
    out = rgb.astype(np.float32).copy()
    for _ in range(DILATE_PASSES):
        if filled.all():
            break
        total = np.zeros_like(out)
        count = np.zeros(filled.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            shifted = np.roll(out, (dy, dx), axis=(0, 1))
            mask = np.roll(filled, (dy, dx), axis=(0, 1))
            total += shifted * mask[..., None]
            count += mask
        grow = (~filled) & (count > 0)
        out[grow] = total[grow] / count[grow][..., None]
        filled = filled | grow
    return out


def box_filter(image, factor):
    """Average `factor` x `factor` blocks. The cell grid is a multiple of the
    factor, so this cannot mix two frames together."""
    h, w, c = image.shape
    return image.reshape(h // factor, factor, w // factor, factor, c).mean(axis=(1, 3))


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    source = Path(sys.argv[1])
    sheet = np.asarray(Image.open(source).convert('RGBA'))
    height, width = sheet.shape[:2]
    if width % CELL or height % CELL:
        raise SystemExit(f'{width}x{height} is not a whole number of {CELL} px cells')
    columns, rows = width // CELL, height // CELL

    alpha = sheet[..., 3]
    # Which cells were actually drawn. A cell with nothing in it is a gap in
    # the sheet's rectangle, not a frame of the animation.
    used = []
    for row in range(rows):
        for column in range(columns):
            cell = alpha[row * CELL:(row + 1) * CELL, column * CELL:(column + 1) * CELL]
            used.append(bool(cell.any()))
    if not used[0]:
        raise SystemExit('the first cell is empty; this is not a sprite sheet')
    # Frames run left to right, top to bottom, and stop at the last drawn cell.
    # An empty cell in the MIDDLE would make the count a lie, so refuse it.
    frames = max(i for i, on in enumerate(used) if on) + 1
    if not all(used[:frames]):
        raise SystemExit('the sheet has an empty cell between two drawn ones')

    rgb = dilate_rgb(sheet[..., :3], alpha)
    cooked = np.concatenate([rgb, alpha[..., None].astype(np.float32)], axis=2)
    factor = CELL // TARGET
    if factor > 1:
        cooked = box_filter(cooked, factor)
    cooked = np.clip(np.rint(cooked), 0, 255).astype(np.uint8)

    OUT_PNG.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(cooked).save(OUT_PNG, optimize=True)

    OUT_HEADER.write_text(f'''#pragma once

#include <cstddef>

namespace apricot {{

// The flame atlas cooked by tools/cook_fire_sprites.py from the supplied fire
// spritesheet. GENERATED — edit the cooker, not this file.
//
// These three numbers live here rather than beside the drawing code because
// the cooker and the renderer must agree about them exactly. An atlas read
// with the wrong column count does not fail: it plays a fire made of halves of
// two frames, which looks like a shader bug and is not one.
//
// Frames run LEFT TO RIGHT, TOP TO BOTTOM. gfx/texture.h flips images
// vertically on load (the UV convention the imported model paint was authored
// in), so row 0 of the file is the TOP of the texture in UV space and the V
// range of a row has to be worked out from the flip — see fire_sprite_uv().
inline constexpr std::size_t kFireSheetColumns = {columns};
inline constexpr std::size_t kFireSheetRows = {rows};
inline constexpr std::size_t kFireSheetFrames = {frames};
inline constexpr int kFireSheetCellPixels = {TARGET};
inline constexpr const char* kFireSheetAsset = "textures/effects/fire_sheet.png";

// Half a texel in, so a bilinear tap at a frame's edge cannot reach into the
// next one. The mip chain still averages further than this at coarse levels,
// which is what the cooker's RGB dilation is for; the inset handles level 0.
inline constexpr float kFireSheetInset =
    0.5f / static_cast<float>(kFireSheetCellPixels);

}}  // namespace apricot
''')

    print(json.dumps(dict(
        source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        source_size=[int(width), int(height)], grid=[int(columns), int(rows)],
        frames=int(frames), empty_cells=int(len(used) - sum(used)),
        cell_pixels=TARGET, dilate_passes=DILATE_PASSES,
        output_size=[int(cooked.shape[1]), int(cooked.shape[0])],
        output=str(OUT_PNG.relative_to(ROOT)),
        output_sha256=hashlib.sha256(OUT_PNG.read_bytes()).hexdigest(),
        output_bytes=OUT_PNG.stat().st_size), indent=2))


if __name__ == '__main__':
    main()
