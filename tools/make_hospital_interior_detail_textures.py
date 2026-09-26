#!/usr/bin/env python3
"""Build deterministic, original Pinatty Regional Hospital interior textures.

This script uses only Pillow and the Python standard library. Material swatches
are periodic and wrap their hand-painted details across all four edges. Signs
and the CRT face are fitted images and are intentionally not tileable.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import random
from pathlib import Path
from typing import Iterable, Sequence

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "assets/textures/world/hospital/interior_detail"
MATERIAL_SIZE = 512
SIGN_SIZE = (1024, 256)
SCREEN_SIZE = (640, 480)

# Each texture has a fixed, separate seed so adding detail to one image never
# changes the other assets.
SEEDS = {
    "terrazzo": 9101,
    "upholstery": 9102,
    "laminate": 9103,
    "curtain": 9104,
    "wallpaint": 9105,
    "ceiling": 9106,
    "steel": 9107,
    "reception-sign": 9111,
    "pharmacy-sign": 9112,
    "diagnostics-sign": 9113,
    "emergency-sign": 9114,
    "ward-sign": 9115,
    "directory-sign": 9116,
    "screen": 9117,
}

FONT_CANDIDATES = {
    "bold": (
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    ),
    "regular": (
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ),
}


def _find_font(style: str, size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    for candidate in FONT_CANDIDATES[style]:
        if Path(candidate).exists():
            return ImageFont.truetype(candidate, size)
    return ImageFont.load_default()


def _smooth(t: float) -> float:
    return t * t * (3.0 - 2.0 * t)


def _value_noise(size: int, cells: int, rng: random.Random) -> list[float]:
    """One octave of seamless value noise, sampled across a periodic lattice."""
    grid = [[rng.random() * 2.0 - 1.0 for _ in range(cells)] for _ in range(cells)]
    x_lattice = []
    for x in range(size):
        gx = x * cells / size
        ix = int(gx)
        tx = _smooth(gx - ix)
        x_lattice.append((ix % cells, (ix + 1) % cells, tx))
    y_lattice = []
    for y in range(size):
        gy = y * cells / size
        iy = int(gy)
        ty = _smooth(gy - iy)
        y_lattice.append((iy % cells, (iy + 1) % cells, ty))

    values: list[float] = []
    for y0, y1, ty in y_lattice:
        row0, row1 = grid[y0], grid[y1]
        for x0, x1, tx in x_lattice:
            a = row0[x0] + (row0[x1] - row0[x0]) * tx
            b = row1[x0] + (row1[x1] - row1[x0]) * tx
            values.append(a + (b - a) * ty)
    return values


def _fractal_field(size: int, seed: int) -> list[int]:
    rng = random.Random(seed)
    octaves = ((3, 0.38), (6, 0.28), (12, 0.18), (24, 0.11), (48, 0.05))
    accum = [0.0] * (size * size)
    for index, (cells, weight) in enumerate(octaves):
        values = _value_noise(size, cells, random.Random(rng.randrange(1 << 30) + index))
        for pixel, value in enumerate(values):
            accum[pixel] += value * weight
    peak = sum(weight for _, weight in octaves)
    return [max(0, min(255, int(127.5 + value / peak * 127.5))) for value in accum]


def _toned_base(size: int, color: tuple[int, int, int], seed: int,
                amplitude: float = 6.0) -> Image.Image:
    field = _fractal_field(size, seed)
    data = []
    for value in field:
        n = (value - 127.5) / 127.5 * amplitude
        data.append(tuple(max(0, min(255, int(c + n))) for c in color))
    image = Image.new("RGB", (size, size))
    image.putdata(data)
    return image


def _wrapped(draw: ImageDraw.ImageDraw, size: tuple[int, int],
             method: str, coords, **kwargs) -> None:
    """Draw a primitive and any edge-crossing copies needed for seamless tiling."""
    width, height = size
    if method in ("ellipse", "rectangle"):
        x0, y0, x1, y1 = coords
        xs = [0]
        ys = [0]
        if x0 < 0:
            xs.append(width)
        if x1 >= width:
            xs.append(-width)
        if y0 < 0:
            ys.append(height)
        if y1 >= height:
            ys.append(-height)
        for sx in xs:
            for sy in ys:
                shifted = (x0 + sx, y0 + sy, x1 + sx, y1 + sy)
                getattr(draw, method)(shifted, **kwargs)
        return

    if method == "polygon":
        points = coords
    else:
        points = None
    xs = [0]
    ys = [0]
    if points:
        min_x = min(p[0] for p in points)
        max_x = max(p[0] for p in points)
        min_y = min(p[1] for p in points)
        max_y = max(p[1] for p in points)
        if min_x < 0:
            xs.append(width)
        if max_x >= width:
            xs.append(-width)
        if min_y < 0:
            ys.append(height)
        if max_y >= height:
            ys.append(-height)
    for sx in xs:
        for sy in ys:
            if method == "polygon":
                shifted = [(x + sx, y + sy) for x, y in coords]
                draw.polygon(shifted, **kwargs)
            elif method == "line":
                shifted = [(x + sx, y + sy) for x, y in coords]
                draw.line(shifted, **kwargs)


def _material_terrazzo() -> Image.Image:
    size, seed = MATERIAL_SIZE, SEEDS["terrazzo"]
    image = _toned_base(size, (212, 207, 190), seed, 5.5)
    draw = ImageDraw.Draw(image)
    rng = random.Random(seed + 90)
    chips = ((164, 169, 157), (184, 178, 160), (144, 151, 146),
             (191, 184, 164), (119, 130, 127), (225, 218, 199),
             (172, 153, 130))
    # Dense but fine aggregate, with a few larger grains to read at game scale.
    for _ in range(490):
        x, y = rng.randrange(size), rng.randrange(size)
        r = rng.choices((rng.uniform(0.7, 1.4), rng.uniform(1.4, 2.8),
                         rng.uniform(2.8, 4.7)), (0.53, 0.38, 0.09))[0]
        rx, ry = r * rng.uniform(0.75, 1.6), r * rng.uniform(0.6, 1.25)
        color = rng.choice(chips)
        if rng.random() < 0.08:
            color = tuple(max(0, min(255, c + rng.choice((-8, 8)))) for c in color)
        _wrapped(draw, image.size, "ellipse",
                 (x - rx, y - ry, x + rx, y + ry), fill=color)
    # Sparse, almost invisible polish swirls give the surface a poured finish.
    for _ in range(18):
        x, y = rng.randrange(size), rng.randrange(size)
        span = rng.randrange(28, 78)
        points = []
        for step in range(9):
            px = x + step * span / 8
            py = y + math.sin(step / 8 * math.tau) * rng.uniform(0.3, 1.6)
            points.append((px, py))
        _wrapped(draw, image.size, "line", points,
                 fill=rng.choice(((201, 198, 184), (218, 213, 197))), width=1)
    return image


def _material_upholstery() -> Image.Image:
    size, seed = MATERIAL_SIZE, SEEDS["upholstery"]
    image = _toned_base(size, (116, 151, 145), seed, 5.0)
    draw = ImageDraw.Draw(image)
    rng = random.Random(seed + 90)
    # A compact plain-weave textile: warp and weft stay periodic at the edges.
    for y in range(0, size, 4):
        for x in range(0, size, 4):
            offset = (x // 4 + y // 4) & 1
            tone = rng.choice((-3, -2, -1, 0, 1, 2, 3))
            c = (113 + tone + offset, 149 + tone + offset,
                 143 + tone + offset)
            draw.line((x, y + (x // 4 & 1), x + 3, y + (x // 4 & 1)),
                      fill=c, width=1)
            draw.line((x + (y // 4 & 1), y, x + (y // 4 & 1), y + 3),
                      fill=(min(255, c[0] + 3), min(255, c[1] + 3), min(255, c[2] + 3)),
                      width=1)
    # Tiny, restrained fiber flecks; all are wrapped at the tile boundary.
    for _ in range(120):
        x, y = rng.randrange(size), rng.randrange(size)
        dx = rng.choice((-2, -1, 1, 2))
        _wrapped(draw, image.size, "line", [(x, y), (x + dx, y + rng.choice((-1, 0, 1)))],
                 fill=rng.choice(((128, 160, 154), (103, 138, 133))), width=1)
    return image


def _material_laminate() -> Image.Image:
    size, seed = MATERIAL_SIZE, SEEDS["laminate"]
    image = _toned_base(size, (190, 166, 126), seed, 5.5)
    draw = ImageDraw.Draw(image)
    rng = random.Random(seed + 90)
    # Pale oak grain follows long, gently wandering fibers and cycles exactly.
    for _ in range(43):
        y0 = rng.randrange(size)
        cycles = rng.choice((1, 2, 3, 4))
        phase = rng.random() * math.tau
        amplitude = rng.uniform(0.4, 2.7)
        points = []
        for i in range(65):
            x = i * (size / 64)
            y = y0 + math.sin(x / size * cycles * math.tau + phase) * amplitude
            points.append((x, y))
        color = rng.choice(((175, 150, 113), (205, 182, 143),
                            (183, 159, 121), (198, 174, 137)))
        _wrapped(draw, image.size, "line", points, fill=color, width=1)
    # A few long, softly faded pores resemble sealed oak without bold knots.
    for _ in range(17):
        x = rng.randrange(size)
        y = rng.randrange(size)
        span = rng.randrange(20, 76)
        color = rng.choice(((174, 150, 116), (209, 187, 150)))
        for offset in (-1, 0, 1):
            _wrapped(draw, image.size, "line",
                     [(x, y + offset), (x + span, y + offset)],
                     fill=color, width=1)
    return image


def _material_curtain() -> Image.Image:
    size, seed = MATERIAL_SIZE, SEEDS["curtain"]
    image = _toned_base(size, (181, 195, 187), seed, 4.2)
    # Soft vertical folds cycle every 128 pixels; fine threads remain visible.
    pix = image.load()
    for y in range(size):
        for x in range(size):
            fold = math.cos((x % 128) / 128 * math.tau) * 4.3
            fine = math.sin((x % 5) / 5 * math.tau + (y % 3) * 0.25) * 1.5
            shadow = fold + fine
            r, g, b = pix[x, y]
            pix[x, y] = (max(0, min(255, int(r + shadow))),
                         max(0, min(255, int(g + shadow))),
                         max(0, min(255, int(b + shadow))))
    draw = ImageDraw.Draw(image)
    # Fold edges are feather-light, not hard stripes.
    for x in (32, 96, 160, 224, 288, 352, 416, 480):
        draw.line((x, 0, x, size - 1), fill=(170, 185, 178), width=1)
        draw.line((x + 2, 0, x + 2, size - 1), fill=(191, 204, 197), width=1)
    return image


def _material_wallpaint() -> Image.Image:
    size, seed = MATERIAL_SIZE, SEEDS["wallpaint"]
    image = _toned_base(size, (221, 217, 202), seed, 4.0)
    draw = ImageDraw.Draw(image)
    rng = random.Random(seed + 90)
    for _ in range(1250):
        x, y = rng.randrange(size), rng.randrange(size)
        color = rng.choice(((213, 210, 197), (225, 221, 206),
                            (218, 215, 202), (230, 226, 212)))
        radius = rng.choices((rng.uniform(0.4, 0.9), rng.uniform(1.0, 1.8)), (0.76, 0.24))[0]
        _wrapped(draw, image.size, "ellipse",
                 (x - radius, y - radius, x + radius, y + radius), fill=color)
    # Very faint wipe/scuff marks. Their low contrast keeps paint clean overall.
    for _ in range(22):
        x, y = rng.randrange(size), rng.randrange(size)
        length = rng.randrange(9, 31)
        slant = rng.choice((-2, -1, 0, 1, 2))
        _wrapped(draw, image.size, "line", [(x, y), (x + length, y + slant)],
                 fill=rng.choice(((210, 208, 195), (228, 225, 211))), width=1)
    return image


def _material_ceiling() -> Image.Image:
    size, seed = MATERIAL_SIZE, SEEDS["ceiling"]
    image = _toned_base(size, (213, 214, 204), seed, 3.2)
    draw = ImageDraw.Draw(image)
    # Low-relief 0.6 m acoustic ceiling panels with an 8 px hole rhythm.
    for x0 in range(0, size, 128):
        for y0 in range(0, size, 128):
            draw.rectangle((x0 + 2, y0 + 2, x0 + 125, y0 + 125),
                           outline=(207, 209, 199), width=1)
            draw.line((x0 + 3, y0 + 3, x0 + 124, y0 + 3),
                      fill=(222, 223, 214), width=1)
            for y in range(y0 + 12, y0 + 120, 8):
                for x in range(x0 + 12, x0 + 120, 8):
                    shade = 198 if ((x // 8 + y // 8) & 1) else 203
                    draw.point((x, y), fill=(shade, shade + 1, shade - 3))
    # Seam relief is aligned to the repeating panel grid at each edge.
    for x in range(0, size, 128):
        draw.line((x, 0, x, size - 1), fill=(204, 206, 197), width=1)
    for y in range(0, size, 128):
        draw.line((0, y, size - 1, y), fill=(204, 206, 197), width=1)
    return image


def _material_steel() -> Image.Image:
    size, seed = MATERIAL_SIZE, SEEDS["steel"]
    image = _toned_base(size, (164, 174, 174), seed, 4.0)
    draw = ImageDraw.Draw(image)
    rng = random.Random(seed + 90)
    # Satin brushed grain: fine horizontal fibers with a few subdued scuffs.
    for y in range(size):
        delta = rng.choice((-4, -3, -2, -1, 0, 1, 2, 3, 4))
        c = tuple(max(0, min(255, channel + delta)) for channel in (164, 174, 174))
        draw.line((0, y, size - 1, y), fill=c, width=1)
    for _ in range(36):
        x, y = rng.randrange(size), rng.randrange(size)
        length = rng.randrange(7, 49)
        _wrapped(draw, image.size, "line", [(x, y), (x + length, y)],
                 fill=rng.choice(((151, 162, 163), (180, 187, 185),
                                  (158, 169, 170))), width=1)
    return image


def make_materials() -> dict[str, Image.Image]:
    return {
        "terrazzo": _material_terrazzo(),
        "upholstery": _material_upholstery(),
        "laminate": _material_laminate(),
        "curtain": _material_curtain(),
        "wallpaint": _material_wallpaint(),
        "ceiling": _material_ceiling(),
        "steel": _material_steel(),
    }


def _sign_background(seed: int) -> Image.Image:
    width, height = SIGN_SIZE
    image = Image.new("RGB", SIGN_SIZE, (228, 226, 211))
    noise = _fractal_field(height, seed)
    draw = ImageDraw.Draw(image)
    # Subtle enamel/laminate speckle without making copy harder to read.
    for y, field in enumerate(noise):
        delta = (field - 127.5) * 0.028
        color = (int(228 + delta), int(226 + delta), int(211 + delta))
        draw.line((0, y, width - 1, y), fill=color)
    draw.rectangle((1, 1, width - 2, height - 2), outline=(193, 197, 185), width=2)
    draw.rectangle((13, 13, width - 14, height - 14), outline=(218, 217, 203), width=1)
    draw.rectangle((25, 23, 35, height - 24), fill=(47, 105, 103))
    draw.rectangle((38, 23, 42, height - 24), fill=(187, 147, 95))
    # Four flush fasteners, well outside the text safe area.
    for x, y in ((20, 20), (width - 21, 20), (20, height - 21), (width - 21, height - 21)):
        draw.ellipse((x - 2, y - 2, x + 2, y + 2), fill=(166, 171, 162))
    return image


def _draw_sign_icon(draw: ImageDraw.ImageDraw, key: str) -> None:
    ink = (56, 111, 108)
    pale = (228, 226, 211)
    gold = (181, 142, 91)
    # A contained 70 px pictogram gives each directory blade a quick scan cue.
    draw.rounded_rectangle((906, 76, 981, 181), radius=5, outline=ink, width=4)
    if key == "reception-sign":
        draw.line((921, 151, 966, 151), fill=ink, width=4)
        draw.rectangle((928, 135, 960, 149), outline=ink, width=3)
        draw.ellipse((938, 112, 950, 124), outline=gold, width=3)
        draw.line((944, 125, 944, 135), fill=gold, width=3)
    elif key == "pharmacy-sign":
        draw.rounded_rectangle((922, 110, 966, 145), radius=17, outline=ink, width=4)
        draw.line((944, 112, 944, 143), fill=gold, width=3)
    elif key == "diagnostics-sign":
        draw.rectangle((920, 101, 966, 154), outline=ink, width=3)
        draw.ellipse((929, 110, 957, 145), outline=gold, width=3)
        draw.line((943, 105, 943, 151), fill=ink, width=2)
    elif key == "emergency-sign":
        draw.line((923, 128, 956, 128), fill=ink, width=5)
        draw.polygon(((953, 113), (973, 128), (953, 143)), fill=gold)
    elif key == "ward-sign":
        draw.line((920, 144, 969, 144), fill=ink, width=4)
        draw.line((924, 143, 924, 157), fill=ink, width=3)
        draw.line((965, 143, 965, 157), fill=ink, width=3)
        draw.rectangle((930, 124, 951, 143), outline=gold, width=3)
        draw.ellipse((923, 121, 931, 129), outline=ink, width=2)
    elif key == "directory-sign":
        draw.rectangle((922, 94, 965, 160), outline=ink, width=3)
        draw.line((944, 96, 944, 158), fill=gold, width=2)
        draw.line((925, 119, 961, 119), fill=ink, width=2)
        draw.line((925, 139, 961, 139), fill=ink, width=2)
        draw.ellipse((937, 128, 943, 134), fill=gold)


SIGN_COPY = {
    "reception-sign": ("RECEPTION", "ADMISSIONS  •  INFORMATION", "PINATTY REGIONAL HOSPITAL"),
    "pharmacy-sign": ("PHARMACY", "PRESCRIPTIONS  •  PICKUP", "PINATTY REGIONAL HOSPITAL"),
    "diagnostics-sign": ("DIAGNOSTIC IMAGING", "X-RAY  •  CT  •  ULTRASOUND", "PINATTY REGIONAL HOSPITAL"),
    "emergency-sign": ("EMERGENCY", "24 HOUR  •  AMBULANCE ENTRANCE", "PINATTY REGIONAL HOSPITAL"),
    "ward-sign": ("PATIENT WARD", "INPATIENT ROOMS", "PINATTY REGIONAL HOSPITAL"),
    "directory-sign": ("HOSPITAL DIRECTORY", "LEVEL 1  •  CLINICS  •  WARDS", "PINATTY REGIONAL HOSPITAL"),
}


def _make_sign(key: str) -> Image.Image:
    image = _sign_background(SEEDS[key])
    draw = ImageDraw.Draw(image)
    title, subtitle, kicker = SIGN_COPY[key]
    title_font = _find_font("bold", 55 if len(title) < 17 else 48)
    subtitle_font = _find_font("bold", 23)
    kicker_font = _find_font("regular", 16)
    draw.text((69, 34), kicker, font=kicker_font, fill=(107, 121, 116), spacing=0)
    draw.text((68, 61), title, font=title_font, fill=(31, 69, 70), spacing=0)
    draw.rectangle((69, 133, 205, 137), fill=(182, 144, 95))
    draw.text((69, 151), subtitle, font=subtitle_font, fill=(70, 102, 100), spacing=0)
    _draw_sign_icon(draw, key)
    # A tiny intentional ink wear, kept far from the letterforms.
    rng = random.Random(SEEDS[key] + 200)
    for _ in range(120):
        x, y = rng.choice((rng.randrange(53, 88), rng.randrange(855, 898))), rng.randrange(27, 228)
        draw.point((x, y), fill=rng.choice(((223, 220, 205), (232, 228, 214))))
    return image


def _make_screen() -> Image.Image:
    width, height = SCREEN_SIZE
    image = Image.new("RGB", SCREEN_SIZE, (9, 21, 21))
    draw = ImageDraw.Draw(image)
    rng = random.Random(SEEDS["screen"])
    green = (126, 190, 147)
    dim_green = (65, 112, 89)
    pale = (180, 201, 179)
    amber = (194, 164, 98)

    # Convincing early-1990s phosphor terminal layout without a surrounding bezel.
    draw.rectangle((15, 15, width - 16, height - 16), outline=dim_green, width=2)
    draw.rectangle((23, 23, width - 24, 65), fill=(13, 35, 32))
    draw.text((36, 30), "PINATTY REGIONAL", font=_find_font("bold", 18), fill=green)
    draw.text((374, 34), "14 AUG 91", font=_find_font("regular", 15), fill=amber)
    draw.text((37, 79), "IMAGING CONSOLE", font=_find_font("bold", 24), fill=pale)
    draw.line((35, 108, width - 36, 108), fill=dim_green, width=1)

    # Raster scan area: a fictional monochrome ultrasound-like slice, not a
    # patient photo. Pixel noise and broad anatomical forms stay behind the UI.
    scan_box = (37, 128, 414, 410)
    draw.rectangle(scan_box, outline=dim_green, width=2)
    draw.rectangle((43, 134, 408, 404), fill=(11, 26, 25))
    # A low-resolution speckle field makes the scan read as an analog medical image.
    scan_rng = random.Random(SEEDS["screen"] + 1)
    for _ in range(2600):
        x = scan_rng.randrange(48, 404)
        y = scan_rng.randrange(139, 400)
        brightness = scan_rng.choices((28, 40, 53, 69, 86), (0.40, 0.30, 0.18, 0.09, 0.03))[0]
        draw.rectangle((x, y, x + scan_rng.choice((0, 1, 2)), y + scan_rng.choice((0, 1))),
                       fill=(brightness, brightness + 11, brightness + 5))
    # Central fan-shaped field and a simple rib-like arc suggest a scan, not a
    # modern digital interface. The image remains deliberately impressionistic.
    draw.polygon(((223, 145), (82, 381), (369, 381)), fill=(17, 38, 35), outline=(89, 143, 111))
    for index in range(7):
        y = 194 + index * 25
        rx = 74 + index * 9
        draw.arc((223 - rx, y - 23, 223 + rx, y + 56), 195, 345,
                 fill=(101 + index * 3, 147 + index * 2, 117 + index * 2), width=2)
    # Strong edge marker and scan sweep line.
    draw.line((87, 344, 363, 344), fill=(151, 185, 140), width=1)
    draw.line((223, 146, 223, 380), fill=(65, 105, 81), width=1)
    draw.text((54, 138), "IMAGE 01", font=_find_font("regular", 11), fill=green)

    # Right-hand controls use large, legible text, blank patient data and no IDs.
    draw.rectangle((431, 128, 601, 410), outline=dim_green, width=2)
    draw.text((448, 145), "MODE", font=_find_font("bold", 16), fill=green)
    draw.text((448, 169), "ULTRASOUND", font=_find_font("regular", 13), fill=pale)
    draw.line((448, 197, 585, 197), fill=dim_green, width=1)
    draw.text((448, 211), "GAIN", font=_find_font("regular", 13), fill=green)
    draw.text((545, 211), "62", font=_find_font("bold", 15), fill=pale)
    draw.rectangle((449, 239, 583, 250), outline=dim_green, width=1)
    draw.rectangle((451, 241, 523, 248), fill=green)
    draw.text((448, 268), "DEPTH", font=_find_font("regular", 13), fill=green)
    draw.text((545, 268), "12 CM", font=_find_font("bold", 14), fill=pale)
    draw.line((448, 296, 585, 296), fill=dim_green, width=1)
    draw.rectangle((448, 312, 583, 350), outline=amber, width=1)
    draw.text((461, 323), "SCAN READY", font=_find_font("bold", 13), fill=amber)
    draw.text((448, 371), "PATIENT:  ----", font=_find_font("regular", 12), fill=dim_green)

    draw.text((39, 428), "SYSTEM READY", font=_find_font("bold", 13), fill=green)
    draw.text((434, 428), "MONOCHROME  /  60 HZ", font=_find_font("regular", 11), fill=dim_green)
    # Phosphor scanlines and a few signal imperfections are embedded in RGB.
    pixels = image.load()
    for y in range(1, height, 3):
        for x in range(width):
            r, g, b = pixels[x, y]
            pixels[x, y] = (max(0, int(r * 0.78)), max(0, int(g * 0.82)), max(0, int(b * 0.80)))
    for _ in range(180):
        x = rng.randrange(25, width - 24)
        y = rng.randrange(20, height - 20)
        if rng.random() < 0.6:
            r, g, b = pixels[x, y]
            pixels[x, y] = (r, min(255, g + 8), min(255, b + 4))
    return image


def build_assets(out_dir: Path, contact_sheet: Path | None = None) -> dict[str, Image.Image]:
    out_dir.mkdir(parents=True, exist_ok=True)
    images = make_materials()
    images.update({key: _make_sign(key) for key in SIGN_COPY})
    images["screen"] = _make_screen()
    for name, image in images.items():
        path = out_dir / f"{name}.png"
        image.save(path, format="PNG", optimize=False, compress_level=9)
        print(f"{path.relative_to(ROOT) if path.is_relative_to(ROOT) else path}: "
              f"{image.width}x{image.height} RGB sha256={hashlib.sha256(path.read_bytes()).hexdigest()}")
    if contact_sheet:
        _save_contact_sheet(images, contact_sheet)
    return images


def _save_contact_sheet(images: dict[str, Image.Image], path: Path) -> None:
    thumb_w, thumb_h = 320, 160
    gap, label_h = 14, 32
    columns = 4
    rows = math.ceil(len(images) / columns)
    sheet = Image.new("RGB", (columns * thumb_w + (columns + 1) * gap,
                              rows * (thumb_h + label_h) + (rows + 1) * gap),
                      (239, 237, 225))
    draw = ImageDraw.Draw(sheet)
    label_font = _find_font("bold", 15)
    for index, (name, image) in enumerate(images.items()):
        col, row = index % columns, index // columns
        x, y = gap + col * (thumb_w + gap), gap + row * (thumb_h + label_h)
        preview = image.copy()
        preview.thumbnail((thumb_w, thumb_h), Image.Resampling.LANCZOS)
        # Each sample is shown at its natural aspect ratio, centered in its cell.
        px = x + (thumb_w - preview.width) // 2
        py = y + (thumb_h - preview.height) // 2
        sheet.paste(preview, (px, py))
        draw.text((x, y + thumb_h + 5), f"{name}  {image.width}x{image.height}",
                  font=label_font, fill=(42, 59, 57))
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path, format="PNG", optimize=False, compress_level=9)
    print(f"contact sheet: {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=OUT_DIR)
    parser.add_argument("--contact-sheet", type=Path)
    args = parser.parse_args()
    build_assets(args.output_dir, args.contact_sheet)


if __name__ == "__main__":
    main()
