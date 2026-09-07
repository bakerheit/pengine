#!/usr/bin/env python3
"""Draw the original Cinder GT pixel atlas, with no external art or fonts.

Run from any directory: python3 tools/make_orison_cinder_texture.py
Use --guide to also write an enlarged, labeled atlas guide beside body.png.
All strokes are deliberate native-resolution pixels; no noise or filtering.
"""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw

from orison_cinder_spec import ATLAS_SIZE, REGIONS, project_xy

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets/textures/vehicles/orison_cinder/body.png"

PALETTE = {
    "gutter": (15, 24, 27, 255),
    "paint_dark": (23, 59, 60, 255),
    "paint_low": (28, 70, 69, 255),
    "paint": (34, 81, 79, 255),
    "paint_upper": (42, 94, 90, 255),
    "paint_light": (49, 105, 99, 255),
    "paint_edge": (63, 119, 111, 255),
    "seam": (22, 54, 56, 255),
    "black": (16, 24, 27, 255),
    "rubber": (23, 29, 31, 255),
    "trim_edge": (39, 49, 50, 255),
    "shadow": (10, 17, 20, 255),
    "glass_dark": (17, 32, 39, 255),
    "glass": (27, 46, 54, 255),
    "glass_light": (45, 67, 75, 255),
    "glass_reflect": (60, 83, 90, 255),
    "metal_dark": (61, 70, 74, 255),
    "metal": (135, 148, 148, 255),
    "metal_light": (202, 209, 196, 255),
    "lamp_dark": (98, 108, 101, 255),
    "lamp": (189, 198, 171, 255),
    "lamp_light": (235, 233, 201, 255),
    "amber_dark": (123, 66, 30, 255),
    "amber": (208, 125, 43, 255),
    "amber_light": (234, 163, 62, 255),
    "red_dark": (94, 29, 35, 255),
    "red": (166, 42, 43, 255),
    "red_light": (207, 63, 54, 255),
    "ivory": (210, 211, 185, 255),
}

# Small original 3x5 block glyphs.  Keeping these literal avoids system font
# drift and makes the plate legible when shown at the intended texel scale.
GLYPHS = {
    "C": ("111", "100", "100", "100", "111"),
    "I": ("111", "010", "010", "010", "111"),
    "N": ("101", "111", "111", "111", "101"),
    "D": ("110", "101", "101", "101", "110"),
    "E": ("111", "100", "110", "100", "111"),
    "R": ("110", "101", "110", "101", "101"),
    "G": ("111", "100", "101", "101", "111"),
    "T": ("111", "010", "010", "010", "010"),
    " ": ("000",) * 5,
}


def build_atlas():
    im = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), PALETTE["gutter"])
    draw = ImageDraw.Draw(im)

    def fill(name, color):
        draw.rectangle(REGIONS[name], fill=PALETTE[color])

    def local_box(name, box, color):
        """Draw in normalized image coordinates within an inclusive cell."""
        x0, y0, x1, y1 = REGIONS[name]
        a, b, c, d = box
        draw.rectangle((round(x0 + a * (x1 - x0)), round(y0 + b * (y1 - y0)),
                        round(x0 + c * (x1 - x0)), round(y0 + d * (y1 - y0))),
                       fill=PALETTE[color])

    def projected_line(name, points, color, width=1):
        draw.line([tuple(round(v) for v in project_xy(name, *point))
                   for point in points], fill=PALETTE[color], width=width)

    def side_band(z0, z1, color):
        a = project_xy("SIDE", -2.21, z1)
        b = project_xy("SIDE", 2.21, z0)
        draw.rectangle(tuple(round(v) for v in (*a, *b)), fill=PALETTE[color])

    # A small number of broad value steps wraps continuously across the side.
    fill("SIDE", "paint")
    side_band(0.76, 0.90, "paint_upper")
    side_band(0.80, 0.86, "paint_light")
    side_band(0.36, 0.48, "paint_low")
    side_band(0.26, 0.36, "paint_dark")
    side_band(0.15, 0.26, "rubber")
    projected_line("SIDE", [(-2.21, 0.265), (2.21, 0.265)], "trim_edge")

    # One understated coupe door seam, no outlines on the underlying triangles.
    door = [(-0.68, 0.80), (-0.65, 0.40), (-0.53, 0.30),
            (0.55, 0.30), (0.67, 0.39), (0.68, 0.80)]
    projected_line("SIDE", door, "seam")
    # A body-colored flush handle with a one-pixel finger recess.
    projected_line("SIDE", [(-0.58, 0.735), (-0.42, 0.735)], "black")
    projected_line("SIDE", [(-0.58, 0.748), (-0.42, 0.748)], "paint_light")
    projected_line("SIDE", [(1.80, 0.58), (1.92, 0.58)], "amber_dark", 2)
    projected_line("SIDE", [(1.81, 0.589), (1.91, 0.589)], "amber")

    # Top paint stays quiet.  Broad cross-car facets carry form without adding
    # fake vent stripes or repeat patterns to hood, roof and deck polygons.
    fill("TOP", "paint_upper")
    local_box("TOP", (0.10, 0, 0.89, 1), "paint_light")
    local_box("TOP", (0.23, 0, 0.77, 1), "paint_upper")
    local_box("TOP", (0.13, 0, 0.15, 1), "paint_edge")
    local_box("TOP", (0.85, 0, 0.87, 1), "paint_edge")

    # Closed pop-up lids remain flush with the faceted hood.  Only their
    # shutlines are painted; the uninterrupted base paint supplies each lid.
    for left, right in ((0.37, 0.77), (-0.77, -0.37)):
        projected_line("TOP", [(left, 1.52), (right, 1.52),
                               (right, 1.93), (left, 1.93), (left, 1.52)], "seam")

    # Each glass polygon owns the complete cell: dark gasket and large, low-
    # contrast reflection blocks stay coherent even on low polygon windows.
    fill("GLASS", "black")
    local_box("GLASS", (0.025, 0.055, 0.975, 0.945), "glass_dark")
    local_box("GLASS", (0.045, 0.08, 0.955, 0.82), "glass")
    gx0, gy0, gx1, gy1 = REGIONS["GLASS"]
    def glass_poly(points, color):
        draw.polygon([(round(gx0 + x * (gx1 - gx0)),
                       round(gy0 + y * (gy1 - gy0))) for x, y in points],
                     fill=PALETTE[color])
    glass_poly([(0.05, 0.10), (0.60, 0.10), (0.955, 0.52), (0.955, 0.69)], "glass_light")
    glass_poly([(0.05, 0.10), (0.19, 0.10), (0.86, 0.78), (0.70, 0.78)], "glass_reflect")
    local_box("GLASS", (0.05, 0.84, 0.95, 0.89), "glass_dark")

    fill("SHADOW", "shadow")
    fill("BLACK", "black")
    local_box("BLACK", (0, 0, 1, 0.09), "trim_edge")
    fill("RUBBER", "rubber")
    local_box("RUBBER", (0, 0.12, 1, 0.24), "trim_edge")
    local_box("RUBBER", (0, 0.79, 1, 1), "black")
    fill("METAL", "metal_dark")
    local_box("METAL", (0, 0.12, 1, 0.47), "metal")
    local_box("METAL", (0, 0.20, 1, 0.27), "metal_light")
    local_box("METAL", (0, 0.70, 1, 0.80), "metal")

    # Recessed lamp cells. Fine rib lines are purposeful lens structure and
    # have only one-pixel contrast, unlike random surface scratches.
    for name, dark, base, light in (
        ("HEADLIGHT", "lamp_dark", "lamp", "lamp_light"),
        ("AMBER", "amber_dark", "amber", "amber_light"),
        ("RED", "red_dark", "red", "red_light"),
        ("REVERSE", "lamp_dark", "ivory", "lamp_light"),
    ):
        fill(name, "black")
        local_box(name, (0.045, 0.09, 0.955, 0.91), dark)
        local_box(name, (0.09, 0.17, 0.91, 0.79), base)
        local_box(name, (0.10, 0.20, 0.90, 0.29), light)
        x0, y0, x1, y1 = REGIONS[name]
        for x in range(x0 + 5, x1 - 2, 6):
            draw.line((x, y0 + 10, x, y1 - 6), fill=PALETTE[dark])

    # An original octagonal O emblem: brushed metal ring and teal core.
    fill("BADGE", "paint_dark")
    bx0, by0, bx1, by1 = REGIONS["BADGE"]
    cx, cy = (bx0 + bx1) // 2, (by0 + by1) // 2
    octagon = [(-5, -8), (5, -8), (8, -5), (8, 5),
               (5, 8), (-5, 8), (-8, 5), (-8, -5)]
    draw.polygon([(cx + x, cy + y) for x, y in octagon], fill=PALETTE["metal"])
    inner = [(-3, -5), (3, -5), (5, -3), (5, 3),
             (3, 5), (-3, 5), (-5, 3), (-5, -3)]
    draw.polygon([(cx + x, cy + y) for x, y in inner], fill=PALETTE["paint_dark"])
    draw.line((cx - 4, cy - 7, cx + 4, cy - 7), fill=PALETTE["metal_light"])

    fill("PLATE", "black")
    local_box("PLATE", (0.025, 0.07, 0.975, 0.93), "ivory")
    px0, py0, px1, py1 = REGIONS["PLATE"]
    label = "CINDER GT"
    scale = 2
    width = (4 * len(label) - 1) * scale
    origin_x = (px0 + px1 - width) // 2 + 1
    origin_y = (py0 + py1 - 5 * scale) // 2 + 1
    for index, letter in enumerate(label):
        for gy, row in enumerate(GLYPHS[letter]):
            for gx, bit in enumerate(row):
                if bit == "1":
                    x, y = origin_x + (index * 4 + gx) * scale, origin_y + gy * scale
                    draw.rectangle((x, y, x + scale - 1, y + scale - 1), fill=PALETTE["black"])
    for x in (px0 + 4, px1 - 4):
        draw.point((x, (py0 + py1) // 2), fill=PALETTE["metal_dark"])

    return im


def build_guide(im):
    guide = im.resize((ATLAS_SIZE * 3, ATLAS_SIZE * 3), Image.Resampling.NEAREST).convert("RGB")
    draw = ImageDraw.Draw(guide)
    for name, (x0, y0, x1, y1) in REGIONS.items():
        box = (x0 * 3, y0 * 3, (x1 + 1) * 3 - 1, (y1 + 1) * 3 - 1)
        draw.rectangle(box, outline=(226, 236, 224), width=1)
        label_box = draw.textbbox((box[0] + 3, box[1] + 3), name)
        draw.rectangle((label_box[0] - 2, label_box[1] - 2, label_box[2] + 2, label_box[3] + 2),
                       fill=(12, 20, 23))
        draw.text((box[0] + 3, box[1] + 3), name, fill=(226, 236, 224))
    return guide


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--guide", action="store_true")
    args = parser.parse_args()
    im = build_atlas()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    im.save(args.output)
    print(f"Saved {args.output} ({im.width}x{im.height} {im.mode}; {len(im.getcolors(65536))} colors)")
    if args.guide:
        guide_path = args.output.with_name("atlas-guide.png")
        build_guide(im).save(guide_path)
        print(f"Saved {guide_path}")


if __name__ == "__main__":
    main()
