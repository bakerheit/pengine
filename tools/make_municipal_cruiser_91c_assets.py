#!/usr/bin/env python3
"""Model-first texture cook, validation and QA for Municipal Cruiser 91C."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from bake_vehicle_surfaces import read_mesh
from municipal_cruiser_91c_spec import (ATLAS_SIZE, DOOR, DRIVER, LAMPS,
                                        REGIONS, SHAPE, UV_FLIP_U,
                                        UV_PROJECTIONS, WHEELS)
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
    "SIDE_DRIVER": (25, 39, 58, 255),
    "SIDE_PASSENGER": (25, 39, 58, 255),
    "BODY_TOP": (34, 49, 68, 255),
    "BODY_FRONT": (22, 34, 51, 255),
    "BODY_REAR": (18, 29, 44, 255),
    "GLASS_SIDE": (38, 65, 80, 148),
    "GLASS_FRONT": (45, 76, 91, 148),
    "GLASS_REAR": (31, 53, 68, 148),
    "BLACK": (10, 13, 17, 255),
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

RAMPS = {
    "navy": [(10, 18, 29), (15, 27, 42), (21, 37, 56), (28, 48, 70),
             (38, 60, 82), (52, 74, 94)],
    "glass": [(12, 29, 39), (18, 40, 52), (25, 54, 68), (35, 69, 83),
              (49, 84, 97), (69, 103, 114)],
    "black": [(5, 7, 9), (9, 12, 15), (14, 18, 22), (20, 24, 28)],
    "metal": [(53, 59, 61), (79, 86, 87), (110, 117, 116),
              (145, 151, 148), (181, 184, 176)],
    "interior": [(14, 16, 19), (23, 26, 30), (34, 37, 40), (47, 49, 50)],
    "seat": [(25, 24, 23), (38, 36, 33), (54, 50, 45), (70, 64, 56)],
}


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
    font = ImageFont.load_default()
    dummy = ImageDraw.Draw(Image.new("1", (1, 1)))
    bounds = dummy.textbbox((0, 0), text, font=font)
    width, height = bounds[2] - bounds[0], bounds[3] - bounds[1]
    mask = Image.new("1", (width, height), 0)
    ImageDraw.Draw(mask).text((-bounds[0], -bounds[1]), text, font=font, fill=1)
    image.paste(colour, xy, mask)


def paint_locked_livery(image: Image.Image) -> None:
    """Overlay exact two-tone markings and critical receivers after reduction."""
    draw = ImageDraw.Draw(image)
    cream = (205, 193, 154, 255)
    cream_hi = (226, 215, 178, 255)
    cream_shadow = (157, 147, 116, 255)
    navy_text = (12, 27, 45, 255)

    for name in ("SIDE_DRIVER", "SIDE_PASSENGER"):
        model_rectangle(draw, name, -2.51, .63, 2.50, .91, cream)
        a = region_point(name, -2.51, .91)
        b = region_point(name, 2.50, .91)
        draw.line((a, b), fill=cream_hi, width=1)
        a = region_point(name, -2.51, .63)
        b = region_point(name, 2.50, .63)
        draw.line((a, b), fill=cream_shadow, width=1)
        box = REGIONS[name]
        stripe_mid = region_point(name, 0, .77)[1]
        label = "POLICE"
        font = ImageFont.load_default()
        probe = ImageDraw.Draw(Image.new("1", (1, 1)))
        tb = probe.textbbox((0, 0), label, font=font)
        text_width, text_height = tb[2] - tb[0], tb[3] - tb[1]
        bitmap_text(image, ((box[0] + box[2] - text_width) // 2,
                            stripe_mid - text_height // 2), label, navy_text)

    # Cream roof over the formal cabin; hood and short deck remain navy.
    model_rectangle(draw, "BODY_TOP", -.83, -1.23, .83, .90, cream)
    draw.line((region_point("BODY_TOP", -.75, -1.16),
               region_point("BODY_TOP", -.75, .82)), fill=cream_hi, width=1)
    draw.line((region_point("BODY_TOP", .75, -1.16),
               region_point("BODY_TOP", .75, .82)), fill=cream_shadow, width=1)

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
        font = ImageFont.load_default()
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


def grade_region(pixels: np.ndarray, name: str, ramp_name: str) -> None:
    x0, y0, x1, y1 = REGIONS[name]
    crop = pixels[y0:y1 + 1, x0:x1 + 1]
    luminance = (crop[:, :, 0].astype(float) * .2126 +
                 crop[:, :, 1].astype(float) * .7152 +
                 crop[:, :, 2].astype(float) * .0722)
    low, high = np.percentile(luminance, (4, 96))
    ramp = np.asarray(RAMPS[ramp_name], dtype=np.uint8)
    if high - low < 1:
        levels = np.full(luminance.shape, len(ramp) // 2, dtype=int)
    else:
        levels = np.rint((luminance - low) / (high - low) *
                         (len(ramp) - 1)).astype(int)
    levels = np.clip(levels, 0, len(ramp) - 1)
    crop[:, :, :3] = ramp[levels]
    crop[:, :, 3] = 255


def semantic_grade(image: Image.Image) -> Image.Image:
    """Keep imagegen luminance clusters but lock material identity."""
    pixels = np.asarray(image.convert("RGBA")).copy()
    for name in ("SIDE_DRIVER", "SIDE_PASSENGER", "BODY_TOP",
                 "BODY_FRONT", "BODY_REAR"):
        grade_region(pixels, name, "navy")
    for name in ("GLASS_SIDE", "GLASS_FRONT", "GLASS_REAR"):
        grade_region(pixels, name, "glass")
    grade_region(pixels, "BLACK", "black")
    grade_region(pixels, "METAL", "metal")
    grade_region(pixels, "INTERIOR", "interior")
    grade_region(pixels, "SEAT", "seat")
    return Image.fromarray(pixels)


def make_texture() -> None:
    if not IMAGEGEN_SOURCE.is_file():
        raise FileNotFoundError(f"built-in imagegen edit missing: {IMAGEGEN_SOURCE}")
    with Image.open(IMAGEGEN_SOURCE) as source:
        source = source.convert("RGB")
        if source.width != source.height:
            raise ValueError(f"imagegen edit must remain square: {source.size}")
        reduced = source.resize((ATLAS_SIZE, ATLAS_SIZE),
                                Image.Resampling.NEAREST)
    composed = template_base().convert("RGB")
    for box in REGIONS.values():
        crop_box = (box[0], box[1], box[2] + 1, box[3] + 1)
        composed.paste(reduced.crop(crop_box), crop_box)
    reduced_palette = composed.quantize(colors=48,
                                        method=Image.Quantize.MEDIANCUT,
                                        dither=Image.Dither.NONE).convert("RGBA")
    image = semantic_grade(reduced_palette)
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


def validate(texture_source: str, require_imagegen: bool = True):
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
            "cook": "nearest 4x reduction, 48-colour no-dither seed, semantic ramps, locked livery and lamps",
        },
        "art_direction": SHAPE["traits"],
        "checks": [
            "joined wheel-less closed body equals all split triangles",
            "four sampled negative-space wheel wells and intact centre hood",
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


def shared_wheels() -> list[Part]:
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
            positions += np.array([side * WHEELS["x"], WHEELS["arch_y"], axle])
            results.append(Part(positions, wheel.normals.copy(), wheel.uvs,
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
