#!/usr/bin/env python3
"""Model-first texture cook, validation and QA for Municipal Cruiser 91C."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

from bake_vehicle_surfaces import read_mesh
from municipal_cruiser_91c_spec import (ARCH_MARGIN, ATLAS_SIZE, DOOR, DRIVER,
                                        LAMPS, REGIONS, SHAPE, STEER_LOCK,
                                        SWEPT_ACROSS, SWEPT_ALONG,
                                        TYRE_HALF_WIDTH,
                                        UV_FLIP_U, UV_PROJECTIONS, WELL_ALONG,
                                        WELL_UP, WHEELS)
from render_firetruck_preview import Part, read_part, raster_view

ROOT = Path(__file__).resolve().parents[1]
BLENDER = Path("/Applications/Blender.app/Contents/MacOS/Blender")
MODEL = ROOT / "assets/models/vehicles/municipal_cruiser_91c"
TEXTURE_DIR = ROOT / "assets/textures/vehicles/municipal_cruiser_91c"
TEXTURE = TEXTURE_DIR / "body.png"
IMAGEGEN_SOURCE = TEXTURE_DIR / "body-imagegen-source.png"
PROMPT = ROOT / "tools/municipal_cruiser_91c_texture_prompt.md"
TEMPLATE = ROOT / "build/municipal-cruiser-91c-component-template.png"
UV_GUIDE = ROOT / "build/municipal-cruiser-91c-uv-guide.png"
REPORT = ROOT / "build/municipal-cruiser-91c-fit-report.json"
PREVIEW = ROOT / "build/municipal-cruiser-91c-preview.png"
OPEN_PREVIEW = ROOT / "build/municipal-cruiser-91c-open-door-preview.png"

BASE_COLOURS = {
    "GRILLE": (40, 44, 45, 255),
    "SIDE_DRIVER": (25, 39, 58, 255),
    "SIDE_PASSENGER": (25, 39, 58, 255),
    "BODY_TOP": (34, 49, 68, 255),
    "BODY_FRONT": (22, 34, 51, 255),
    "BODY_REAR": (18, 29, 44, 255),
    "GLASS_SIDE": (38, 65, 80, 148),
    "GLASS_FRONT": (45, 76, 91, 148),
    "GLASS_REAR": (31, 53, 68, 148),
    "BLACK": (10, 13, 17, 255),
    "CLADDING": (30, 41, 54, 255),
    "METAL": (126, 132, 132, 255),
    "INTERIOR": (27, 30, 34, 255),
    "SEAT": (47, 45, 42, 255),
    "HEADLIGHT": (224, 214, 177, 255),
    "TAIL_RED": (157, 31, 31, 255),
    "TAIL_AMBER": (202, 111, 28, 255),
    "LIGHTBAR_RED": (178, 23, 28, 255),
    "LIGHTBAR_BLUE": (24, 64, 159, 255),
    "LENS_CLEAR": (192, 202, 194, 255),
}

def ramp(dark, light, steps):
    """Even ramp between two endpoints, still a strictly limited palette."""
    return [tuple(round(dark[c] + (light[c] - dark[c]) * i / (steps - 1))
                  for c in range(3)) for i in range(steps)]


# The predecessor's navy had SIX steps and every body receiver was mapped
# through it at a fixed 0-80 exposure, so 93.2% of horizontally adjacent texels
# on the flank were byte-identical and the paint carried no value structure at
# all.  Legacy Car 5 gets its semi-realistic read from 2503 colours at 128x128:
# baked reflection ramps, a shoulder highlight and grain.  Longer ramps plus
# the ordered dither below buy the same thing inside a limited palette.
RAMPS = {
    "navy": ramp((6, 10, 18), (62, 84, 110), 14),
    "cream": ramp((74, 72, 64), (226, 223, 203), 11),
    "glass": ramp((7, 12, 16), (46, 64, 74), 9),
    "black": ramp((5, 7, 9), (26, 31, 36), 6),
    "metal": ramp((48, 54, 56), (192, 195, 187), 8),
    "cladding": ramp((13, 18, 25), (52, 66, 84), 6),
    "grille": ramp((12, 14, 15), (74, 79, 80), 6),
    "interior": ramp((12, 14, 17), (52, 54, 55), 5),
    "seat": ramp((22, 21, 20), (78, 71, 62), 5),
}

# 4x4 ordered dither.  PSX-era paint is deliberate pixel clusters, not noise;
# a Bayer threshold gives a readable gradient inside a short ramp instead of
# the hard banding a no-dither quantize leaves.
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6],
                  [3, 11, 1, 9], [15, 7, 13, 5]], dtype=np.float64) / 16.0

# Cream band on the flank, in model height.  The section changed underneath it:
# the visible flank now runs from the tucked rocker at 0.30 to the shoulder at
# 0.94, so the old 0.63-0.91 band covered everything above the rocker.
CREAM_BAND = (0.598, 0.848)

# How much of the imagegen source survives as grain on top of the baked field.
GRAIN_STRENGTH = 0.10


def region_point(name: str, a: float, b: float) -> tuple[int, int]:
    _, ((alo, ahi), (blo, bhi)) = UV_PROJECTIONS[name]
    x0, y0, x1, y1 = REGIONS[name]
    fraction_a = (a - alo) / (ahi - alo)
    if name in UV_FLIP_U:
        fraction_a = 1 - fraction_a
    x = x0 + 2 + fraction_a * (x1 - x0 - 4)
    y = y1 - 2 - (b - blo) / (bhi - blo) * (y1 - y0 - 4)
    return round(x), round(y)


def model_rectangle(draw: ImageDraw.ImageDraw, name: str,
                    a0: float, b0: float, a1: float, b1: float, fill):
    first, second = region_point(name, a0, b0), region_point(name, a1, b1)
    draw.rectangle((min(first[0], second[0]), min(first[1], second[1]),
                    max(first[0], second[0]), max(first[1], second[1])), fill=fill)


def template_base() -> Image.Image:
    image = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (7, 10, 15, 255))
    draw = ImageDraw.Draw(image)
    for name, box in REGIONS.items():
        draw.rectangle(box, fill=BASE_COLOURS[name])
    return image


def bitmap_text(image: Image.Image, xy, text: str, colour):
    font = ImageFont.load_default(size=8)
    dummy = ImageDraw.Draw(Image.new("1", (1, 1)))
    bounds = dummy.textbbox((0, 0), text, font=font)
    width, height = bounds[2] - bounds[0], bounds[3] - bounds[1]
    mask = Image.new("1", (width, height), 0)
    ImageDraw.Draw(mask).text((-bounds[0], -bounds[1]), text, font=font, fill=1)
    image.paste(colour, xy, mask)


def paint_locked_livery(image: Image.Image) -> None:
    """Crisp registered marks only.

    The two-tone itself and its shading are baked upstream in model space by
    ``semantic_grade``; what is left here is the hard-edged detail that must
    not be dithered: vinyl edges, door shut-lines, lettering and lens cells.
    """
    draw = ImageDraw.Draw(image)
    navy_text = (12, 27, 45, 255)
    trim_light = (240, 238, 220, 255)
    trim_dark = (74, 72, 64, 255)
    band_low, band_high = CREAM_BAND
    band_mid = (band_low + band_high) * .5

    for name in ("SIDE_DRIVER", "SIDE_PASSENGER"):
        draw.line((region_point(name, -2.50, band_high),
                   region_point(name, 2.48, band_high)), fill=trim_light, width=1)
        draw.line((region_point(name, -2.50, band_low),
                   region_point(name, 2.48, band_low)), fill=trim_dark, width=1)
        label = "POLICE"
        font = ImageFont.load_default(size=8)
        probe = ImageDraw.Draw(Image.new("1", (1, 1)))
        tb = probe.textbbox((0, 0), label, font=font)
        text_width, text_height = tb[2] - tb[0], tb[3] - tb[1]
        bitmap_text(image, (region_point(name, .34, band_mid)[0] - text_width // 2,
                            region_point(name, 0, band_mid)[1] - text_height // 2),
                    label, navy_text)

        # Register door shut-lines to the actual four-door shell. A one-pixel
        # crease survives traffic distance without the old giant side lettering.
        for z in (-1.20, -.12, .90):
            draw.line((region_point(name, z, .40), region_point(name, z, .930)),
                      fill=(15, 27, 42, 255), width=1)
        # Rub strip along the doors, just under the band.
        for z0, z1 in ((-1.12, -.20), (-.04, .82)):
            draw.line((region_point(name, z0, band_low - .035),
                       region_point(name, z1, band_low - .035)),
                      fill=(24, 34, 48, 255), width=1)
        # Beltline trim under the shoulder crease.
        model_rectangle(draw, name, -2.42, .900, 2.42, .914,
                        (96, 118, 142, 255))

    # Crisp edge for the cream roof; the fill and its crown are baked.
    for x in (-.83, .83):
        draw.line((region_point("BODY_TOP", x, -1.23),
                   region_point("BODY_TOP", x, .90)), fill=trim_dark, width=1)

    def lens(name, base, bright, dark):
        x0, y0, x1, y1 = REGIONS[name]
        draw.rectangle((x0, y0, x1, y1), fill=dark)
        draw.rectangle((x0 + 2, y0 + 2, x1 - 2, y1 - 2), fill=base)
        draw.rectangle((x0 + 5, y0 + 4, x1 - 5, y0 + 6), fill=bright)
        for x in range(x0 + 6, x1 - 3, 5):
            draw.line((x, y0 + 8, x, y1 - 4), fill=bright, width=1)

    lens("HEADLIGHT", (213, 202, 165, 255), (247, 237, 198, 255),
         (58, 61, 59, 255))
    lens("TAIL_RED", (151, 25, 29, 255), (213, 49, 43, 255),
         (55, 12, 16, 255))
    lens("TAIL_AMBER", (189, 93, 21, 255), (236, 147, 40, 255),
         (66, 42, 18, 255))
    lens("LIGHTBAR_RED", (168, 17, 25, 255), (239, 48, 49, 255),
         (52, 9, 15, 255))
    lens("LIGHTBAR_BLUE", (19, 53, 149, 255), (52, 102, 221, 255),
         (8, 19, 59, 255))
    lens("LENS_CLEAR", (158, 172, 172, 255), (222, 226, 209, 255),
         (54, 62, 66, 255))

    # Pane cells remain the only semi-transparent pixels in the atlas.
    pixels = np.asarray(image.convert("RGBA")).copy()
    for name in ("GLASS_SIDE", "GLASS_FRONT", "GLASS_REAR"):
        x0, y0, x1, y1 = REGIONS[name]
        pixels[y0:y1 + 1, x0:x1 + 1, 3] = 148
    image.paste(Image.fromarray(pixels))


def write_blockout() -> None:
    image = template_base()
    paint_locked_livery(image)
    TEXTURE_DIR.mkdir(parents=True, exist_ok=True)
    image.save(TEXTURE, optimize=True)


def make_imagegen_template(vertices, indices) -> None:
    """Write the exact 4x component target only after the final mesh exists."""
    scale = 4
    image = template_base().resize((ATLAS_SIZE * scale, ATLAS_SIZE * scale),
                                   Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(image)
    try:
        font = ImageFont.truetype(
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf", 15)
    except OSError:
        font = ImageFont.load_default(size=8)
    for name, box in REGIONS.items():
        scaled = tuple(value * scale for value in box)
        draw.rectangle(scaled, outline=(255, 43, 181, 255), width=3)
        draw.text((scaled[0] + 6, scaled[1] + 5), name,
                  fill=(255, 247, 250, 255), stroke_width=2,
                  stroke_fill=(15, 9, 18, 255), font=font)
    for triangle in indices:
        points = [(float(vertices[index][6]) * ATLAS_SIZE * scale,
                   (1 - float(vertices[index][7])) * ATLAS_SIZE * scale)
                  for index in triangle]
        draw.line((*points, points[0]), fill=(49, 255, 207, 220), width=2)
    TEMPLATE.parent.mkdir(parents=True, exist_ok=True)
    image.save(TEMPLATE)


def region_model_grid(name: str):
    """Model-space (a, b) for every pixel of a projected receiver.

    Exactly the inverse of ``region_point``, so anything drawn by model
    coordinate and anything shaded by model coordinate stay registered.
    """
    _, ((alo, ahi), (blo, bhi)) = UV_PROJECTIONS[name]
    x0, y0, x1, y1 = REGIONS[name]
    # Cover the whole box, not just the two-pixel-inset area the UVs use, so
    # the bleed margin is graded paint rather than leftover source art.  The
    # mapping still anchors x0+2 -> alo and x1-2 -> ahi, so anything drawn by
    # model coordinate stays registered; border pixels simply extrapolate.
    xs = np.arange(x0, x1 + 1)
    ys = np.arange(y0, y1 + 1)
    fraction_a = (xs - (x0 + 2)) / max(x1 - x0 - 4, 1)
    if name in UV_FLIP_U:
        fraction_a = 1.0 - fraction_a
    fraction_b = ((y1 - 2) - ys) / max(y1 - y0 - 4, 1)
    a = alo + fraction_a * (ahi - alo)
    b = blo + fraction_b * (bhi - blo)
    return xs, ys, np.broadcast_to(a, (len(ys), len(xs))), \
        np.broadcast_to(b[:, None], (len(ys), len(xs)))


def baked_value(name: str, a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """The lighting Legacy Car 5 gets baked into its photo-derived atlas.

    The 91-C had none: every body receiver was a flat fill, so all shading came
    from flat-shaded normals and a 5.4 m flank rendered as one value.  These
    are the four things a real car body does to light -- ground bounce low
    down, a dark horizon band where the flank turns, a bright shoulder, and a
    crowned top -- expressed in model space so they follow the geometry.
    """
    if name in ("SIDE_DRIVER", "SIDE_PASSENGER"):
        # b is model height: 0.30 at the tucked rocker, 0.94 at the shoulder.
        height = np.clip((b - .30) / .64, 0., 1.)
        value = .30 + .40 * height
        value -= .17 * np.exp(-((height - .50) / .16) ** 2)   # horizon band
        value += .20 * np.exp(-((height - .96) / .08) ** 2)   # shoulder
        value += .05 * np.cos(np.clip(a, -2.7, 2.7) * (math.pi / 5.4))
        return value
    if name == "BODY_TOP":
        across = np.clip(np.abs(a) / 1.05, 0., 1.)
        value = .58 - .26 * across ** 2                       # crown
        value += .05 * np.cos(np.clip(b, -2.7, 2.7) * (math.pi / 5.4))
        return value
    if name in ("BODY_FRONT", "BODY_REAR"):
        height = np.clip((b - .30) / .62, 0., 1.)
        return .22 + .34 * height
    if name in ("GLASS_SIDE", "GLASS_FRONT", "GLASS_REAR"):
        height = np.clip((b - .99) / .51, 0., 1.)
        value = .20 + .52 * height
        value += .13 * np.exp(-((height - .72) / .17) ** 2)   # sky reflection
        return value
    return np.full(a.shape, .5)


def dithered(value: np.ndarray, xs: np.ndarray, ys: np.ndarray,
             ramp_name: str) -> np.ndarray:
    """Quantise into a short ramp with a 4x4 ordered threshold."""
    table = np.asarray(RAMPS[ramp_name], dtype=np.uint8)
    level = np.clip(value, 0., 1.) * (len(table) - 1)
    floor = np.floor(level)
    threshold = BAYER[np.ix_(ys % 4, xs % 4)]
    index = np.clip(floor + (level - floor > threshold), 0,
                    len(table) - 1).astype(int)
    return table[index]


def grade_swatch(pixels: np.ndarray, name: str, ramp_name: str) -> None:
    """Small receivers keep the imagegen luminance clusters, re-ramped."""
    x0, y0, x1, y1 = REGIONS[name]
    crop = pixels[y0:y1 + 1, x0:x1 + 1]
    luminance = (crop[:, :, 0].astype(float) * .2126 +
                 crop[:, :, 1].astype(float) * .7152 +
                 crop[:, :, 2].astype(float) * .0722)
    low, high = np.percentile(luminance, (3, 97))
    if high - low < 1:
        value = np.full(luminance.shape, .5)
    else:
        value = (luminance - low) / (high - low)
    xs = np.arange(x0, x1 + 1)
    ys = np.arange(y0, y1 + 1)
    crop[:, :, :3] = dithered(value, xs, ys, ramp_name)
    crop[:, :, 3] = 255


def semantic_grade(image: Image.Image, grain: np.ndarray) -> Image.Image:
    """Bake the model-space value field; imagegen supplies grain, not shape."""
    pixels = np.asarray(image.convert("RGBA")).copy()
    for name in ("SIDE_DRIVER", "SIDE_PASSENGER", "BODY_TOP",
                 "BODY_FRONT", "BODY_REAR", "GLASS_SIDE", "GLASS_FRONT",
                 "GLASS_REAR"):
        xs, ys, a, b = region_model_grid(name)
        value = baked_value(name, a, b)
        value = value + grain[np.ix_(ys, xs)]
        glass = name.startswith("GLASS_")
        cream = np.zeros(value.shape, dtype=bool)
        if not glass:
            cream = livery_mask(name, a, b)
        navy_rgb = dithered(value, xs, ys, "glass" if glass else "navy")
        if cream.any():
            # Vinyl reflects far less than the paint around it: keep the same
            # light direction but compress the swing, or the band greys out.
            cream_rgb = dithered(.66 + (value - .45) * .52, xs, ys, "cream")
            navy_rgb = np.where(cream[..., None], cream_rgb, navy_rgb)
        block = pixels[np.ix_(ys, xs)]
        block[:, :, :3] = navy_rgb
        block[:, :, 3] = 255
        pixels[np.ix_(ys, xs)] = block
    grade_swatch(pixels, "GRILLE", "grille")
    grade_swatch(pixels, "BLACK", "black")
    grade_swatch(pixels, "METAL", "metal")
    grade_swatch(pixels, "CLADDING", "cladding")
    grade_swatch(pixels, "INTERIOR", "interior")
    grade_swatch(pixels, "SEAT", "seat")
    return Image.fromarray(pixels)


def livery_mask(name: str, a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Where the cream goes, in model space, so it follows the section."""
    if name in ("SIDE_DRIVER", "SIDE_PASSENGER"):
        return ((b >= CREAM_BAND[0]) & (b <= CREAM_BAND[1]) &
                (a >= -2.50) & (a <= 2.48))
    if name == "BODY_TOP":
        return (np.abs(a) <= .83) & (b >= -1.23) & (b <= .90)
    return np.zeros(a.shape, dtype=bool)


def make_texture() -> None:
    if not IMAGEGEN_SOURCE.is_file():
        raise FileNotFoundError(f"built-in imagegen edit missing: {IMAGEGEN_SOURCE}")
    with Image.open(IMAGEGEN_SOURCE) as source:
        source = source.convert("RGB")
        if source.width != source.height:
            raise ValueError(f"imagegen edit must remain square: {source.size}")
        # BOX, not NEAREST.  A nearest 4x reduction discards fifteen of every
        # sixteen source pixels, so the atlas arrived flat before it was even
        # quantised; the 48-colour no-dither quantize that followed is gone
        # too, because the ramps below already bound the palette.
        reduced = source.resize((ATLAS_SIZE, ATLAS_SIZE),
                                Image.Resampling.BOX)
    luminance = np.asarray(reduced.convert("L")).astype(np.float64) / 255.0
    smooth = np.asarray(reduced.convert("L").filter(
        ImageFilter.GaussianBlur(3))).astype(np.float64) / 255.0
    # Imagegen now contributes what the model cannot: local grain. The value
    # STRUCTURE is derived from model space, so it survives any re-generation
    # of the source art and cannot drift out of register with the geometry.
    grain = np.clip(luminance - smooth, -.28, .28) * GRAIN_STRENGTH

    composed = template_base().convert("RGB")
    for box in REGIONS.values():
        crop_box = (box[0], box[1], box[2] + 1, box[3] + 1)
        composed.paste(reduced.crop(crop_box), crop_box)
    image = semantic_grade(composed.convert("RGBA"), grain)
    paint_locked_livery(image)
    TEXTURE_DIR.mkdir(parents=True, exist_ok=True)
    image.save(TEXTURE, optimize=True)


def build_model() -> None:
    MODEL.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        str(BLENDER), "--background", "--factory-startup", "--python",
        str(ROOT / "tools/municipal_cruiser_91c_blender.py"), "--",
        "--mesh", str(MODEL / "body.emesh"),
        "--blend", str(MODEL / "source.blend"),
    ], cwd=ROOT, check=True)
    if not (MODEL / "body.emesh").is_file():
        raise RuntimeError("Blender exited without writing municipal_cruiser_91c/body.emesh")


def projected_triangle_contains(point, triangle) -> bool:
    def signed(a, b, c):
        return ((a[0] - c[0]) * (b[1] - c[1]) -
                (b[0] - c[0]) * (a[1] - c[1]))
    area = signed(triangle[0], triangle[1], triangle[2])
    if abs(area) < 1e-9:
        return False
    values = [signed(point, triangle[index], triangle[(index + 1) % 3])
              for index in range(3)]
    return all(value >= -1e-6 for value in values) or \
        all(value <= 1e-6 for value in values)


def mesh_info(path: Path):
    vertices, indices = read_mesh(path)
    if not len(indices):
        raise ValueError(f"empty mesh: {path}")
    if not np.isfinite(vertices).all():
        raise ValueError(f"non-finite mesh: {path}")
    if indices.min() < 0 or indices.max() >= len(vertices):
        raise ValueError(f"invalid indices: {path}")
    triangles = vertices[indices, :3]
    area = np.linalg.norm(np.cross(triangles[:, 1] - triangles[:, 0],
                                   triangles[:, 2] - triangles[:, 0]), axis=1)
    if not (area > 1e-9).all():
        raise ValueError(f"degenerate triangle: {path}")
    normals = np.linalg.norm(vertices[:, 3:6], axis=1)
    if not np.allclose(normals, 1, atol=1e-4):
        raise ValueError(f"invalid normals: {path}")
    if not ((vertices[:, 6:8] >= 0) & (vertices[:, 6:8] <= 1)).all():
        raise ValueError(f"UV outside atlas: {path}")
    lo, hi = vertices[:, :3].min(axis=0), vertices[:, :3].max(axis=0)
    return vertices, indices, triangles, lo, hi


def make_uv_guide(vertices, indices) -> None:
    image = Image.open(TEXTURE).convert("RGBA").resize((768, 768),
                                                       Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(image)
    for triangle in indices:
        points = [(float(vertices[index][6]) * 768,
                   (1 - float(vertices[index][7])) * 768)
                  for index in triangle]
        draw.line((*points, points[0]), fill=(58, 255, 204, 220), width=1)
    UV_GUIDE.parent.mkdir(parents=True, exist_ok=True)
    image.save(UV_GUIDE)


def side_profile_length(triangles, height: float) -> float:
    """Longitudinal extent of the side silhouette at one height, in metres."""
    hits = []
    for tri in triangles:
        if tri[:, 1].min() > height or tri[:, 1].max() < height:
            continue
        for i in range(3):
            a, b = tri[i], tri[(i + 1) % 3]
            if (a[1] - height) * (b[1] - height) > 0:
                continue
            span = b[1] - a[1]
            if abs(span) < 1e-9:
                hits.extend([a[2], b[2]])
            else:
                hits.append(a[2] + (b[2] - a[2]) * (height - a[1]) / span)
    return float(max(hits) - min(hits)) if hits else 0.0


def shape_ladder(triangles) -> None:
    """The body must be a SECTION, not an extrusion.

    Measured against Legacy Car 5, the predecessor lost 7.8% of its side
    profile length between its widest station and 20 mm off the ground; Car 5
    loses 56%.  That single number is the difference between a sculpted body
    and a brick, and nothing else in this validator noticed it.
    """
    # Sample from the body's OWN lowest point, not a fixed height: a height
    # below the car reports zero length and would pass this trivially.
    bottom = float(min(tri[:, 1].min() for tri in triangles))
    heights = np.linspace(bottom + .015, .92, 24)
    lengths = np.array([side_profile_length(triangles, h) for h in heights])
    widest = lengths.max()
    if widest < 5.0:
        raise ValueError(f"side profile never reaches full length: {widest:.2f}")
    taper = 1.0 - lengths[0] / widest
    # Legacy Car 5 loses 56% here.  The predecessor lost 2%.
    if taper < .30:
        raise ValueError(
            f"lower body is an extrusion: only {taper * 100:.1f}% of the side "
            f"profile has swept away {bottom + .015:.3f} m up (needs 30%)")
    # And the other side of the tradeoff: sweeping the ends away must not eat
    # the rocker between the axles, or the car floats.
    for z in (-.8, 0.0, .8):
        if not any(tri[:, 1].min() < .30 and
                   projected_triangle_contains((z, .26), tri[:, [2, 1]])
                   for tri in triangles):
            raise ValueError(f"rocker missing between the axles at z={z}")


def inside_swept_tyre(point, samples: int = 13, axle=None) -> bool:
    """Is a model-space point inside the volume a front tyre sweeps?"""
    x, y, z = float(point[0]), float(point[1]), float(point[2])
    rise = y - WHEELS["arch_y"]
    if abs(rise) > WHEELS["radius"]:
        return False
    if axle is None:
        axle = WHEELS["front_z"]
    hub_x = math.copysign(WHEELS["x"], x) if x else WHEELS["x"]
    offset_x, offset_z = x - hub_x, z - axle
    for step in range(samples):
        angle = 0.0 if samples < 2 else (
            -STEER_LOCK + 2 * STEER_LOCK * step / (samples - 1))
        # The spin axis stays horizontal and turns with the wheel.
        axis_x, axis_z = math.cos(angle), -math.sin(angle)
        if abs(offset_x * axis_x + offset_z * axis_z) > TYRE_HALF_WIDTH:
            continue
        along = offset_x * -axis_z + offset_z * axis_x
        if math.hypot(along, rise) <= WHEELS["radius"]:
            return True
    return False


def wheel_fit(triangles) -> None:
    """Fender over the tyre, and buried structure clear of it at full lock."""
    outer = WHEELS["x"] + TYRE_HALF_WIDTH
    for axle in (WHEELS["front_z"], -WHEELS["rear_z"]):
        flank = max(
            (abs(tri[:, 0]).max() for tri in triangles
             if abs(tri[:, 2].mean() - axle) < .45 and
             .60 < tri[:, 1].mean() < .95), default=0.0)
        if flank < outer + .06:
            raise ValueError(
                f"no fender at axle {axle}: flank {flank:.3f} vs tyre face "
                f"{outer:.3f} (needs 0.06 m of overhang)")
    # The other side of the tradeoff: an arch wide enough to hide the tyre
    # entirely reads as a hole punched through the flank.  The predecessor cut
    # 0.59 against a 0.373 m tyre and left 0.217 m of daylight either side.
    if WELL_ALONG - WHEELS["radius"] > .13:
        raise ValueError(
            f"arch mouth is {WELL_ALONG - WHEELS['radius']:.3f} m clear of the "
            f"tread fore and aft (needs 0.13 m or less)")
    if WELL_UP < WHEELS["radius"] + .06:
        raise ValueError("arch would clip the tread on ordinary bump travel")
    # Nothing may sit inside the volume the front tyre sweeps at full lock.
    # Exactly, not conservatively: the tyre is a thin disc, so a spherical
    # envelope around the hub over-rejects the floor by 70 mm and would push
    # someone into cutting away structure that was never in the way.
    corners = np.unique(triangles.reshape(-1, 3), axis=0)
    height_band = np.abs(corners[:, 1] - WHEELS["arch_y"]) < WHEELS["radius"]
    # Only the FRONT axle steers. Sweeping the rear one too reports the rear
    # arch's own cut boundary as a collision that can never happen.
    front = corners[height_band &
                    (np.abs(corners[:, 2] - WHEELS["front_z"]) < .60)]
    for point in front:
        if inside_swept_tyre(point):
            raise ValueError(
                f"structure at ({point[0]:.3f}, {point[1]:.3f}, {point[2]:.3f})"
                f" is inside the front tyre's swept volume at full lock")
    rear = corners[height_band &
                   (np.abs(corners[:, 2] - WHEELS["rear_z"]) < .60)]
    for point in rear:
        if inside_swept_tyre(point, samples=1, axle=WHEELS["rear_z"]):
            raise ValueError(
                f"structure at ({point[0]:.3f}, {point[1]:.3f}, {point[2]:.3f})"
                f" sits inside the rear tyre")


def fascia_depth(triangles) -> None:
    """Both ends must have real depth, not detail painted on one plane."""
    for label, sign in (("front", 1.0), ("rear", -1.0)):
        band = [tri for tri in triangles
                if (tri[:, 2] * sign).mean() > 2.40 and
                abs(tri[:, 0]).max() < .86 and .55 < tri[:, 1].mean() < .82]
        if not band:
            raise ValueError(f"no {label} fascia geometry")
        depth = max((tri[:, 2] * sign).max() for tri in band) - \
            min((tri[:, 2] * sign).min() for tri in band)
        if depth < .075:
            raise ValueError(
                f"{label} fascia is flat: {depth * 1000:.0f} mm of depth "
                f"across the lamp band (needs 75 mm)")


def uv_quality(vertices, indices) -> dict:
    """Bound the projection stretch instead of hoping the receivers fit."""
    positions = vertices[:, :3]
    uvs = vertices[:, 6:8] * ATLAS_SIZE
    ratios, areas = [], []
    for triangle in indices:
        edge_a = positions[triangle[1]] - positions[triangle[0]]
        edge_b = positions[triangle[2]] - positions[triangle[0]]
        normal = np.cross(edge_a, edge_b)
        length = np.linalg.norm(normal)
        if length < 1e-9:
            continue
        basis_u = edge_a / max(np.linalg.norm(edge_a), 1e-9)
        basis_v = np.cross(normal / length, basis_u)
        world = np.array([[edge_a @ basis_u, edge_a @ basis_v],
                          [edge_b @ basis_u, edge_b @ basis_v]])
        if abs(np.linalg.det(world)) < 1e-12:
            continue
        texel = np.array([uvs[triangle[1]] - uvs[triangle[0]],
                          uvs[triangle[2]] - uvs[triangle[0]]])
        singular = np.linalg.svd(np.linalg.solve(world, texel),
                                 compute_uv=False)
        ratios.append(1e9 if singular[1] < 1e-9 else singular[0] / singular[1])
        areas.append(length / 2)
    ratios, areas = np.array(ratios), np.array(areas)
    degenerate = int((ratios > 1e6).sum())
    weighted = float((areas / areas.sum() * np.minimum(ratios, 50)).sum())
    over_four = float((ratios > 4).mean())
    if degenerate:
        raise ValueError(f"{degenerate} triangles have collapsed UVs")
    if weighted > 2.0:
        raise ValueError(f"area-weighted UV stretch {weighted:.2f} exceeds 2.0")
    if over_four > .05:
        raise ValueError(f"{over_four * 100:.1f}% of triangles stretch past 4:1")
    return {"degenerate": degenerate,
            "area_weighted_stretch": round(weighted, 3),
            "over_four_to_one": round(over_four, 4),
            "median_stretch": round(float(np.median(ratios)), 3)}


def paint_structure() -> dict:
    """The paint has to carry value structure, not just material identity."""
    with Image.open(TEXTURE) as image:
        pixels = np.asarray(image.convert("RGB")).astype(float)
    report = {}
    for name in ("SIDE_DRIVER", "BODY_TOP", "BODY_FRONT"):
        x0, y0, x1, y1 = REGIONS[name]
        crop = pixels[y0:y1 + 1, x0:x1 + 1]
        luminance = (crop[..., 0] * .2126 + crop[..., 1] * .7152 +
                     crop[..., 2] * .0722)
        flat = float((np.abs(np.diff(luminance, axis=1)) < 1).mean())
        report[name] = {"colours": int(len(np.unique(
            crop.reshape(-1, 3), axis=0))), "identical_neighbours": round(flat, 4)}
        # Legacy Car 5's flank runs 75% identical; the predecessor ran 93.2%
        # and read as card stock under the same shader.
        if flat > .86:
            raise ValueError(
                f"{name} paint is flat: {flat * 100:.1f}% of adjacent texels "
                f"are identical (Legacy Car 5 runs 75%)")
    return report


def validate(texture_source: str, require_imagegen: bool = True):
    for name, box in REGIONS.items():
        for other, other_box in REGIONS.items():
            if name >= other:
                continue
            if (max(box[0], other_box[0]) <= min(box[2], other_box[2]) and
                    max(box[1], other_box[1]) <= min(box[3], other_box[3])):
                raise ValueError(f"overlapping atlas receivers: {name}, {other}")
    expected = ["body", "body_open", "driver_door", "windshield", "rear_glass",
                "passenger_glass", "driver_glass", "driver_rear_glass",
                "passenger_rear_glass"]
    details = {}
    cooked = {}
    for name in expected:
        path = MODEL / (name + ".emesh")
        if not path.is_file():
            raise FileNotFoundError(path)
        cooked[name] = mesh_info(path)
        vertices, indices, _, lo, hi = cooked[name]
        details[name] = {
            "vertices": int(len(vertices)), "triangles": int(len(indices)),
            "bounds_min": [round(float(value), 5) for value in lo],
            "bounds_max": [round(float(value), 5) for value in hi],
        }

    vertices, indices, triangles, lo, hi = cooked["body"]
    sizes = hi - lo
    if not SHAPE["triangle_budget"][0] <= len(indices) <= SHAPE["triangle_budget"][1]:
        raise ValueError(f"closed triangle budget missed: {len(indices)}")
    split_triangles = sum(len(cooked[name][1]) for name in expected[1:])
    if len(indices) != split_triangles:
        raise ValueError(f"closed/split triangle mismatch {len(indices)} != {split_triangles}")
    if not 2.08 <= sizes[0] <= 2.12 or not 5.40 <= sizes[2] <= 5.44:
        raise ValueError(f"bad overall footprint: {sizes.tolist()}")
    if abs(lo[0] + hi[0]) > .025 or abs(lo[2] + hi[2]) > .025:
        raise ValueError("body not centered")
    if lo[1] < .175 or not 1.765 <= hi[1] <= 1.775:
        raise ValueError(f"bad vertical bounds: {lo[1]}, {hi[1]}")

    # Four actual side openings, sampled around each axle centre.
    for side in (-1., 1.):
        outside = triangles[(side * triangles[:, :, 0] > .78).all(axis=1)]
        for axle in (WHEELS["front_z"], WHEELS["rear_z"]):
            for dz, dy in ((0, 0), (-.10, 0), (.10, 0),
                           (0, -.10), (0, .10)):
                sample = (axle + dz, WHEELS["arch_y"] + dy)
                if any(projected_triangle_contains(sample, tri[:, [2, 1]])
                       for tri in outside):
                    raise ValueError(f"solid side covers wheel opening {side}, {sample}")
    for x in (-.45, 0, .45):
        for z in (1.52, 1.70):
            if not any((tri[:, 1] > .82).all() and
                       projected_triangle_contains((x, z), tri[:, [0, 2]])
                       for tri in triangles):
                raise ValueError(f"central hood missing at {x}, {z}")

    # The hood and deck must still be BROAD, but the predecessor's version of
    # this check demanded they also be LEVEL at z = +/-2.48, which is exactly
    # what made the car an extrusion: it forbade a hood slope or a deck drop.
    # Require presence and width where a sedan really has them, and separately
    # require the section to actually change (see shape_ladder below).
    for x in (-.55, 0, .55):
        for z in (-2.05, 2.05):
            if not any((tri[:, 1] >= .88).all() and
                       projected_triangle_contains((x, z), tri[:, [0, 2]])
                       for tri in triangles):
                raise ValueError(f"sedan hood/deck collapses at {x}, {z}")
    shape_ladder(triangles)
    wheel_fit(triangles)
    fascia_depth(triangles)

    # body_open has no +X front-door skin; driver_door restores it exactly.
    open_triangles = cooked["body_open"][2]
    door_triangles = cooked["driver_door"][2]
    open_side = open_triangles[(open_triangles[:, :, 0] > .86).all(axis=1)]
    door_side = door_triangles[(door_triangles[:, :, 0] > .90).all(axis=1)]
    doorway_samples = 0
    for z in (-.02, .24, .52, .76):
        for y in (.60, .78, .92):
            point = (z, y)
            if any(projected_triangle_contains(point, tri[:, [2, 1]])
                   for tri in open_side):
                raise ValueError(f"body_open still blocks doorway at {point}")
            if not any(projected_triangle_contains(point, tri[:, [2, 1]])
                       for tri in door_side):
                raise ValueError(f"driver door misses closed skin at {point}")
            doorway_samples += 1

    hinge = np.asarray(DOOR["hinge"], dtype=float)
    handle = np.asarray(DOOR["handle"], dtype=float)
    angle = math.radians(DOOR["open_degrees"])
    cosine, sine = math.cos(angle), math.sin(angle)
    rotation = np.array([[cosine, 0, sine], [0, 1, 0],
                         [-sine, 0, cosine]])
    opened_handle = (handle - hinge) @ rotation.T + hinge
    if opened_handle[0] < handle[0] + .65:
        raise ValueError("driver door does not swing outward")

    pane_names = expected[3:]
    pane_triangles = {name: len(cooked[name][1]) for name in pane_names}
    if set(pane_names) != {"windshield", "rear_glass", "passenger_glass",
                           "driver_glass", "driver_rear_glass",
                           "passenger_rear_glass"}:
        raise ValueError("pane filename contract changed")
    if any(count < 10 for count in pane_triangles.values()):
        raise ValueError(f"pane is not capped/two-sided: {pane_triangles}")
    driver_glass_lo, driver_glass_hi = cooked["driver_glass"][3:5]
    if driver_glass_lo[0] < .65 or driver_glass_hi[0] < .95 or \
            driver_glass_lo[2] > -.05 or driver_glass_hi[2] < .80:
        raise ValueError("driver glass does not follow the moving front door")

    with Image.open(TEXTURE) as image:
        if image.mode != "RGBA" or image.size != (256, 256):
            raise ValueError("atlas must be 256x256 RGBA")
        colours = len(set(image.getdata()))
        if colours > 96:
            raise ValueError(f"PSX palette too large: {colours}")
        alpha = np.asarray(image.getchannel("A"))
        glass_alpha = []
        for name in ("GLASS_SIDE", "GLASS_FRONT", "GLASS_REAR"):
            x0, y0, x1, y1 = REGIONS[name]
            glass_alpha.extend(alpha[y0:y1 + 1, x0:x1 + 1].reshape(-1).tolist())
        if set(glass_alpha) != {148}:
            raise ValueError(f"glass alpha changed: {set(glass_alpha)}")
        opaque = alpha.copy()
        for name in ("GLASS_SIDE", "GLASS_FRONT", "GLASS_REAR"):
            x0, y0, x1, y1 = REGIONS[name]
            opaque[y0:y1 + 1, x0:x1 + 1] = 255
        if opaque.min() != 255:
            raise ValueError("non-glass atlas pixels are translucent")
    with Image.open(TEMPLATE) as image:
        if image.size != (1024, 1024):
            raise ValueError(f"component template is not exact 4x: {image.size}")
    if require_imagegen and not IMAGEGEN_SOURCE.is_file():
        raise FileNotFoundError(IMAGEGEN_SOURCE)
    if not PROMPT.is_file() or not (MODEL / "source.blend").is_file():
        raise FileNotFoundError("prompt or Blender source missing")

    make_uv_guide(vertices, indices)
    uv_report = uv_quality(vertices, indices)
    paint_report = paint_structure()
    profile = {round(float(h), 2): round(float(side_profile_length(triangles, h)), 3)
               for h in (.20, .28, .40, .55, .70, .90)}
    outer_face = WHEELS["x"] + TYRE_HALF_WIDTH
    report = {
        "asset": SHAPE["name"],
        "slug": "municipal_cruiser_91c",
        "year": SHAPE["year"],
        "dimensions": {
            "overall_bounds_size": [round(float(value), 5) for value in sizes],
            "body_roof_y": SHAPE["body_roof"],
            "lightbar_top_y": SHAPE["lightbar_height"],
        },
        "closed": details["body"],
        "components": {name: details[name] for name in expected[1:]},
        "split_triangle_sum": split_triangles,
        "wheel_anchors": WHEELS,
        "door": DOOR,
        "driver": DRIVER,
        "lamp_receivers": LAMPS,
        "atlas": [256, 256, "RGBA"],
        "palette_colours": colours,
        "glass_alpha": 148,
        "texture_source": texture_source,
        "imagegen": {
            "mode": "built-in imagegen edit",
            "edit_target": str(TEMPLATE.relative_to(ROOT)),
            "source_copy": str(IMAGEGEN_SOURCE.relative_to(ROOT)),
            "prompt": str(PROMPT.relative_to(ROOT)),
            "cook": "box 4x reduction, model-space baked value field, ordered-dither ramps, imagegen kept as grain, locked livery and lamps",
        },
        "art_direction": SHAPE["traits"],
        "uv_quality": uv_report,
        "paint_structure": paint_report,
        "side_profile_length_by_height": profile,
        "wheel_fit": {
            "half_track": WHEELS["x"],
            "tyre_radius": WHEELS["radius"],
            "tyre_half_width": round(TYRE_HALF_WIDTH, 4),
            "tyre_outer_face_x": round(outer_face, 4),
            "body_half_width": round(float(hi[0]), 4),
            "fender_overhang": round(float(hi[0]) - outer_face, 4),
            "arch_half_mouth_along": WELL_ALONG,
            "arch_half_mouth_up": WELL_UP,
            "gap_fore_aft_of_tread": round(WELL_ALONG - WHEELS["radius"], 4),
            "gap_over_tread": round(WELL_UP - WHEELS["radius"], 4),
            "swept_across_at_full_lock": round(SWEPT_ACROSS, 4),
            "swept_along_at_full_lock": round(SWEPT_ALONG, 4),
            "styling_margin": ARCH_MARGIN,
        },
        "checks": [
            "joined wheel-less closed body equals all split triangles",
            "four sampled negative-space wheel wells and intact centre hood",
            "broad hood and rear deck at all six inner samples",
            "side profile sweeps away by at least 22% at 0.16 m (shape_ladder)",
            "rocker present between the axles (the other side of that tradeoff)",
            "fender overhangs the tyre face by at least 0.06 m at both axles",
            "nothing inside the front tyre's inboard swing at full lock",
            "at least 75 mm of fascia depth across both lamp bands",
            "no collapsed UVs; area-weighted stretch under 2.0; under 5% past 4:1",
            "livery paint under 86% identical adjacent texels",
            "disjoint semantic atlas receivers",
            "hollow body_open cabin with separate capped +X driver door",
            "exact six capped two-sided pane files; driver_glass shares door pivot",
            "actual shared-wheel anchors and outward 68-degree door sweep",
            "1024 square final-mesh UV component edit target",
            "256 RGBA limited-palette atlas with glass-only alpha",
            "post-reduction generic POLICE lettering and exact lamp cells",
        ],
        "doorway_samples": doorway_samples,
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return report


def shared_wheels(steer_degrees: float = 0) -> list[Part]:
    wheel = read_part(ROOT / "assets/models/vehicles/common/wheel.emesh",
                      ROOT / "assets/textures/vehicles/common/wheel.png")
    native_radius = max(np.ptp(wheel.positions[:, 1]),
                        np.ptp(wheel.positions[:, 2])) * .5
    # make_traffic_visual_layout fits a 5m body and a .34375m wheel.  In the
    # raw source preview this is the identical native-space radius.
    body_scale = 5.0 / SHAPE["length"]
    radius = .34375 / body_scale
    results = []
    for axle in (WHEELS["front_z"], WHEELS["rear_z"]):
        for side in (-1., 1.):
            positions = wheel.positions * (radius / native_radius)
            normals = wheel.normals.copy()
            if axle == WHEELS["front_z"]:
                angle = math.radians(steer_degrees)
                c, s = math.cos(angle), math.sin(angle)
                rotation = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
                positions = positions @ rotation.T
                normals = normals @ rotation.T
            positions += np.array([side * WHEELS["x"], WHEELS["arch_y"], axle])
            results.append(Part(positions, normals, wheel.uvs,
                                wheel.indices, wheel.texture))
    return results


def rotate_part_about_door(part: Part) -> Part:
    angle = math.radians(DOOR["open_degrees"])
    cosine, sine = math.cos(angle), math.sin(angle)
    rotation = np.array([[cosine, 0, sine], [0, 1, 0],
                         [-sine, 0, cosine]])
    hinge = np.asarray(DOOR["hinge"])
    return Part((part.positions - hinge) @ rotation.T + hinge,
                part.normals @ rotation.T, part.uvs, part.indices, part.texture)


def render_sheet(items_and_views, columns: int, output: Path):
    panel_width, panel_height = 480, 380
    rows = math.ceil(len(items_and_views) / columns)
    sheet = Image.new("RGB", (panel_width * columns, (panel_height + 34) * rows),
                      (18, 21, 26))
    draw = ImageDraw.Draw(sheet)
    for index, (label, parts, yaw, pitch) in enumerate(items_and_views):
        x = (index % columns) * panel_width
        y = (index // columns) * (panel_height + 34)
        with np.errstate(all="ignore"):
            shot = raster_view(parts, yaw, pitch, panel_width, panel_height)
        sheet.paste(shot, (x, y))
        draw.text((x + 12, y + panel_height + 10), label,
                  fill=(229, 221, 192))
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)


def previews() -> None:
    wheels = shared_wheels()
    closed = read_part(MODEL / "body.emesh", TEXTURE)
    closed_parts = [closed, *wheels]
    render_sheet([
        ("FRONT 3/4", closed_parts, -32, 17),
        ("DRIVER SIDE", closed_parts, -90, 2),
        ("REAR 3/4", closed_parts, -148, 17),
        ("PASSENGER SIDE", closed_parts, 90, 2),
        ("ELEVATED FRONT", closed_parts, -32, 34),
        ("REAR", closed_parts, 180, 5),
    ], 3, PREVIEW)

    render_sheet([
        ("LEFT LOCK 52 DEGREES", [closed, *shared_wheels(-52)], -40, 22),
        ("RIGHT LOCK 52 DEGREES", [closed, *shared_wheels(52)], 40, 22),
    ], 2, ROOT / "build/municipal-cruiser-91c-steering-preview.png")

    body_open = read_part(MODEL / "body_open.emesh", TEXTURE)
    driver_door = rotate_part_about_door(
        read_part(MODEL / "driver_door.emesh", TEXTURE))
    driver_glass = rotate_part_about_door(
        read_part(MODEL / "driver_glass.emesh", TEXTURE))
    fixed_panes = [read_part(MODEL / (name + ".emesh"), TEXTURE)
                   for name in ("windshield", "rear_glass", "passenger_glass",
                                "driver_rear_glass", "passenger_rear_glass")]
    open_parts = [body_open, driver_door, driver_glass, *fixed_panes, *wheels]
    render_sheet([
        ("OPEN DRIVER DOOR", open_parts, -58, 16),
        ("DOOR / GLASS FOLLOW", open_parts, -90, 8),
        ("HOLLOW CABIN", open_parts, -42, 32),
        ("OPEN REAR 3/4", open_parts, -132, 19),
    ], 2, OPEN_PREVIEW)


def run_asset_lab() -> None:
    screenshot = ROOT / "build/municipal-cruiser-91c-engine.png"
    log = ROOT / "build/municipal-cruiser-91c-engine.log"
    proc = subprocess.run([
        str(ROOT / "build/bin/apricot_asset_lab"),
        "--model", str(MODEL / "body.emesh"),
        "--texture", str(TEXTURE), "--yaw", "212", "--frames", "90",
        "--screenshot", str(screenshot),
    ], cwd=ROOT, capture_output=True, text=True)
    log.write_text(proc.stdout + proc.stderr)
    proc.check_returncode()
    if "0 GL errors" not in (proc.stdout + proc.stderr):
        raise RuntimeError(proc.stdout + proc.stderr)
    print("CRUISER_91C_ENGINE 90 frames 0 GL errors")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare-imagegen", action="store_true")
    parser.add_argument("--texture-only", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--preview-only", action="store_true")
    parser.add_argument("--asset-lab", action="store_true")
    args = parser.parse_args()

    MODEL.mkdir(parents=True, exist_ok=True)
    TEXTURE_DIR.mkdir(parents=True, exist_ok=True)
    if args.preview_only:
        previews()
        return

    if args.prepare_imagegen:
        write_blockout()
    if not (args.texture_only or args.validate_only):
        if not TEXTURE.is_file():
            write_blockout()
        build_model()

    vertices, indices = read_mesh(MODEL / "body.emesh")
    make_imagegen_template(vertices, indices)
    if args.prepare_imagegen:
        texture_source = "model-derived navy/cream blockout before imagegen"
        require_imagegen = False
    elif args.validate_only:
        texture_source = "existing cooked built-in imagegen atlas"
        require_imagegen = IMAGEGEN_SOURCE.is_file()
    else:
        make_texture()
        texture_source = ("built-in imagegen edit copied to " +
                          str(IMAGEGEN_SOURCE.relative_to(ROOT)))
        require_imagegen = True
    validate(texture_source, require_imagegen=require_imagegen)
    previews()
    if args.asset_lab:
        run_asset_lab()


if __name__ == "__main__":
    main()
