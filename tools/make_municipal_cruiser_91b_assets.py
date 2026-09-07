#!/usr/bin/env python3
"""Model-first cook, imagegen atlas reduction and QA for Cruiser 91-B."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from bake_vehicle_surfaces import read_mesh
from municipal_cruiser_91b_spec import (ATLAS_SIZE, BRAKELIGHTS, DOOR,
                                        DRIVER, GLASS_NAMES, HEADLIGHTS,
                                        LIGHTBAR, REGIONS, SHAPE,
                                        UV_PROJECTIONS, WHEELS)
from render_firetruck_preview import Part, raster_view, read_part


ROOT = Path(__file__).resolve().parents[1]
BLENDER = Path("/Applications/Blender.app/Contents/MacOS/Blender")
MODEL = ROOT / "assets/models/vehicles/municipal_cruiser_91b"
TEXTURE_DIR = ROOT / "assets/textures/vehicles/municipal_cruiser_91b"
TEXTURE = TEXTURE_DIR / "body.png"
IMAGEGEN_SOURCE = TEXTURE_DIR / "body-imagegen-source.png"
IMAGEGEN_TEMPLATE = ROOT / "build/municipal-cruiser-91b-imagegen-template.png"
UV_GUIDE = ROOT / "build/municipal-cruiser-91b-uv-guide.png"
PREVIEW = ROOT / "build/municipal-cruiser-91b-preview.png"
OPEN_PREVIEW = ROOT / "build/municipal-cruiser-91b-open-door-preview.png"
BLOCKOUT_PREVIEW = ROOT / "build/municipal-cruiser-91b-blockout.png"
REPORT = ROOT / "build/municipal-cruiser-91b-fit-report.json"
PROMPT = ROOT / "tools/municipal_cruiser_91b_texture_prompt.md"


BASE_COLOURS = {
    "BODY_SIDE": (216, 215, 202, 255),
    "BODY_TOP": (232, 230, 215, 255),
    "BODY_FRONT": (205, 205, 194, 255),
    "BODY_REAR": (190, 192, 184, 255),
    "GLASS_SIDE": (29, 51, 66, 170),
    "GLASS_FRONT": (37, 64, 78, 170),
    "GLASS_REAR": (24, 43, 57, 170),
    "NAVY": (15, 34, 58, 255),
    "WHITE": (218, 218, 207, 255),
    "POLICE_DRIVER": (223, 222, 209, 255),
    "POLICE_PASSENGER": (223, 222, 209, 255),
    "RUBBER": (13, 18, 24, 255),
    "METAL": (128, 133, 132, 255),
    "INTERIOR": (31, 36, 42, 255),
    "SEAM": (35, 43, 51, 255),
    "HEADLIGHT": (224, 218, 187, 255),
    "AMBER": (215, 128, 35, 255),
    "TAIL": (155, 29, 31, 255),
    "REVERSE": (211, 211, 193, 255),
    "LIGHTBAR_RED": (175, 28, 34, 255),
    "LIGHTBAR_BLUE": (30, 67, 151, 255),
}


WHITE_RAMP = np.asarray([
    (104, 108, 108, 255), (132, 134, 130, 255),
    (160, 160, 152, 255), (187, 187, 177, 255),
    (207, 206, 194, 255), (224, 222, 207, 255),
    (238, 235, 218, 255), (247, 244, 226, 255),
], dtype=np.uint8)
NAVY_RAMP = np.asarray([
    (6, 13, 24, 255), (8, 20, 35, 255), (11, 29, 49, 255),
    (15, 39, 65, 255), (21, 52, 82, 255), (31, 68, 99, 255),
], dtype=np.uint8)
GLASS_RAMP = np.asarray([
    (8, 17, 25, 150), (12, 25, 35, 150), (17, 35, 47, 150),
    (24, 48, 62, 150), (34, 64, 78, 150), (50, 83, 96, 150),
    (68, 101, 112, 150),
], dtype=np.uint8)
METAL_RAMP = np.asarray([
    (45, 50, 52, 255), (70, 75, 76, 255), (96, 101, 101, 255),
    (124, 129, 128, 255), (152, 156, 153, 255), (179, 180, 173, 255),
], dtype=np.uint8)
DARK_RAMP = np.asarray([
    (6, 9, 13, 255), (10, 15, 20, 255), (16, 22, 28, 255),
    (23, 31, 38, 255), (32, 41, 48, 255), (45, 55, 61, 255),
], dtype=np.uint8)


def source_to_region(name, a, b):
    _, ((alo, ahi), (blo, bhi)) = UV_PROJECTIONS[name]
    x0, y0, x1, y1 = REGIONS[name]
    x = x0 + 2 + (a - alo) / (ahi - alo) * (x1 - x0 - 4)
    y = y1 - 2 - (b - blo) / (bhi - blo) * (y1 - y0 - 4)
    return round(x), round(y)


def component_base():
    image = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (9, 13, 18, 255))
    draw = ImageDraw.Draw(image)
    for name, box in REGIONS.items():
        draw.rectangle(box, fill=BASE_COLOURS[name])
    # Intended large livery groups give imagegen a useful semantic edit target.
    x0, y0, x1, y1 = REGIONS["BODY_SIDE"]
    lower_top = source_to_region("BODY_SIDE", 0, .63)[1]
    stripe_top = source_to_region("BODY_SIDE", 0, .78)[1]
    stripe_bottom = source_to_region("BODY_SIDE", 0, .69)[1]
    draw.rectangle((x0, lower_top, x1, y1), fill=BASE_COLOURS["NAVY"])
    draw.rectangle((x0, stripe_top, x1, stripe_bottom),
                   fill=BASE_COLOURS["NAVY"])
    for name in ("GLASS_SIDE", "GLASS_FRONT", "GLASS_REAR"):
        x0, y0, x1, y1 = REGIONS[name]
        draw.line((x0 + 5, y1 - 5, x1 - 5, y0 + 6),
                  fill=(65, 96, 108, 190), width=2)
        draw.line((x0 + 4, y0 + 5, x1 - 5, y0 + 5),
                  fill=(85, 113, 121, 185), width=1)
    return image


def _font(size):
    candidates = [
        ROOT / "assets/fonts/BebasNeue-Regular.ttf",
        Path("/System/Library/Fonts/Supplemental/Arial Bold.ttf"),
    ]
    for path in candidates:
        try:
            return ImageFont.truetype(str(path), size)
        except OSError:
            pass
    return ImageFont.load_default()


def paint_locked_details(image):
    """Overlay exact generic markings and critical lens cells after reduction."""
    draw = ImageDraw.Draw(image)

    def rgba(rgb):
        return (*rgb, 255) if image.mode == "RGBA" else rgb

    for name in ("POLICE_DRIVER", "POLICE_PASSENGER"):
        x0, y0, x1, y1 = REGIONS[name]
        draw.rectangle((x0, y0, x1, y1), fill=rgba((222, 221, 207)))
        draw.rectangle((x0 + 1, y0 + 1, x1 - 1, y1 - 1),
                       outline=rgba((19, 47, 76)), width=1)
        font = _font(23)
        text = "POLICE"
        bounds = draw.textbbox((0, 0), text, font=font, stroke_width=0)
        width, height = bounds[2] - bounds[0], bounds[3] - bounds[1]
        position = ((x0 + x1 - width) // 2,
                    (y0 + y1 - height) // 2 - bounds[1])
        # Threshold the TrueType mask so exact lettering adds one colour, not
        # dozens of anti-aliased shades to the final console palette.
        mask = Image.new("L", image.size, 0)
        ImageDraw.Draw(mask).text(position, text, font=font, fill=255)
        mask = mask.point(lambda value: 255 if value >= 128 else 0)
        image.paste(rgba((13, 39, 70)), mask=mask)

    patterns = {
        "HEADLIGHT": ((219, 216, 189), (249, 239, 195), (58, 63, 64)),
        "AMBER": ((185, 91, 21), (244, 166, 47), (64, 42, 25)),
        "TAIL": ((126, 20, 25), (211, 48, 43), (48, 15, 19)),
        "REVERSE": ((184, 187, 174), (239, 232, 204), (55, 61, 61)),
    }
    for name, (base, bright, edge) in patterns.items():
        x0, y0, x1, y1 = REGIONS[name]
        draw.rectangle((x0, y0, x1, y1), fill=rgba(base))
        draw.rectangle((x0 + 1, y0 + 1, x1 - 1, y1 - 1),
                       outline=rgba(edge), width=1)
        for x in range(x0 + 4, x1 - 1, 5):
            draw.rectangle((x, y0 + 3, min(x + 1, x1 - 2), y1 - 3),
                           fill=rgba(bright))
        draw.line((x0 + 3, y0 + 3, x1 - 3, y0 + 3),
                  fill=rgba(bright), width=1)

    for name, base, bright, dark in (
        ("LIGHTBAR_RED", (146, 22, 31), (238, 54, 58), (55, 10, 17)),
        ("LIGHTBAR_BLUE", (21, 51, 130), (57, 113, 222), (8, 20, 60)),
    ):
        x0, y0, x1, y1 = REGIONS[name]
        draw.rectangle((x0, y0, x1, y1), fill=rgba(base))
        draw.rectangle((x0, y0, x1, y1), outline=rgba(dark), width=1)
        for x in range(x0 + 3, x1, 4):
            draw.line((x, y0 + 2, x, y1 - 2), fill=rgba(bright), width=1)
        draw.line((x0 + 2, y0 + 3, x1 - 2, y0 + 3),
                  fill=rgba(bright), width=1)


def make_blockout_texture():
    image = component_base()
    paint_locked_details(image)
    TEXTURE_DIR.mkdir(parents=True, exist_ok=True)
    image.save(TEXTURE)


def make_imagegen_template(vertices, indices):
    """Create the exact 4x edit target from the already-final cooked mesh."""
    scale = 4
    image = component_base().resize((ATLAS_SIZE * scale, ATLAS_SIZE * scale),
                                    Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(image)
    font = _font(15)
    for name, box in REGIONS.items():
        scaled = tuple(value * scale for value in box)
        draw.rectangle(scaled, outline=(255, 48, 186, 255), width=3)
        draw.text((scaled[0] + 6, scaled[1] + 5), name,
                  fill=(255, 247, 252, 255), stroke_width=2,
                  stroke_fill=(13, 8, 18, 255), font=font)
    for triangle in indices:
        points = [(float(vertices[index][6]) * ATLAS_SIZE * scale,
                   (1 - float(vertices[index][7])) * ATLAS_SIZE * scale)
                  for index in triangle]
        draw.line((*points, points[0]), fill=(52, 255, 206, 215), width=2)
    IMAGEGEN_TEMPLATE.parent.mkdir(parents=True, exist_ok=True)
    image.save(IMAGEGEN_TEMPLATE)


def apply_ramp(pixels, name, ramp, selector=None):
    x0, y0, x1, y1 = REGIONS[name]
    crop = pixels[y0:y1 + 1, x0:x1 + 1]
    luminance = (crop[:, :, 0].astype(float) * .2126 +
                 crop[:, :, 1].astype(float) * .7152 +
                 crop[:, :, 2].astype(float) * .0722)
    low, high = np.percentile(luminance, (3, 97))
    if high - low < 1:
        levels = np.full(luminance.shape, len(ramp) // 2, dtype=int)
    else:
        levels = np.rint((luminance - low) / (high - low) *
                         (len(ramp) - 1)).astype(int)
    levels = np.clip(levels, 0, len(ramp) - 1)
    mapped = ramp[levels]
    if selector is None:
        crop[:] = mapped
    else:
        crop[selector] = mapped[selector]


def grade_patrol_texture(image):
    """Retain imagegen value clusters while locking readable fleet colours."""
    pixels = np.asarray(image.convert("RGBA")).copy()
    apply_ramp(pixels, "BODY_TOP", WHITE_RAMP)
    apply_ramp(pixels, "BODY_FRONT", WHITE_RAMP)
    apply_ramp(pixels, "BODY_REAR", WHITE_RAMP)
    apply_ramp(pixels, "WHITE", WHITE_RAMP)
    apply_ramp(pixels, "NAVY", NAVY_RAMP)
    apply_ramp(pixels, "METAL", METAL_RAMP)
    for name in ("RUBBER", "INTERIOR", "SEAM"):
        apply_ramp(pixels, name, DARK_RAMP)
    for name in ("GLASS_SIDE", "GLASS_FRONT", "GLASS_REAR"):
        apply_ramp(pixels, name, GLASS_RAMP)

    # BODY_SIDE uses its final source-space vertical projection: white upper
    # body, deep navy lower door, and a narrow readable navy belt stripe.
    name = "BODY_SIDE"
    x0, y0, x1, y1 = REGIONS[name]
    height = y1 - y0 + 1
    ys = np.arange(y0, y1 + 1)[:, None]
    _, ((_, _), (up_lo, up_hi)) = UV_PROJECTIONS[name]
    usable = max(1, y1 - y0 - 4)
    up = up_lo + ((y1 - 2 - ys) / usable) * (up_hi - up_lo)
    navy_mask = np.broadcast_to((up <= .63) | ((up >= .69) & (up <= .78)),
                                (height, x1 - x0 + 1))
    apply_ramp(pixels, name, WHITE_RAMP)
    apply_ramp(pixels, name, NAVY_RAMP, navy_mask)

    # The low face bands wrap the livery around the clipped nose and trunk.
    for name in ("BODY_FRONT", "BODY_REAR"):
        x0, y0, x1, y1 = REGIONS[name]
        usable = max(1, y1 - y0 - 4)
        ys = np.arange(y0, y1 + 1)[:, None]
        _, ((_, _), (up_lo, up_hi)) = UV_PROJECTIONS[name]
        up = up_lo + ((y1 - 2 - ys) / usable) * (up_hi - up_lo)
        mask = np.broadcast_to(up <= .45, (y1 - y0 + 1, x1 - x0 + 1))
        apply_ramp(pixels, name, NAVY_RAMP, mask)

    image.paste(Image.fromarray(pixels))


def add_modest_wear(image):
    """Fixed one-to-three-pixel clusters, concentrated below the doors."""
    draw = ImageDraw.Draw(image)
    side = REGIONS["BODY_SIDE"]
    chips = ((19, 53, 2), (42, 51, 1), (67, 54, 2), (101, 52, 1),
             (139, 55, 2), (176, 53, 1), (213, 54, 2), (235, 50, 1))
    for offset, y, length in chips:
        x = side[0] + offset
        draw.line((x, side[1] + y, x + length, side[1] + y),
                  fill=(57, 55, 49, 255), width=1)


def cook_imagegen_texture():
    if not IMAGEGEN_SOURCE.is_file():
        raise FileNotFoundError(
            f"built-in imagegen edit missing: {IMAGEGEN_SOURCE}; run --prepare-imagegen first")
    with Image.open(IMAGEGEN_SOURCE) as source:
        source = source.convert("RGB")
        if source.width != source.height:
            raise ValueError(f"imagegen source must stay square: {source.size}")
        reduced = source.resize((ATLAS_SIZE, ATLAS_SIZE), Image.Resampling.BOX)
    composed = Image.new("RGB", (ATLAS_SIZE, ATLAS_SIZE), (9, 13, 18))
    for box in REGIONS.values():
        crop = (box[0], box[1], box[2] + 1, box[3] + 1)
        composed.paste(reduced.crop(crop), crop)
    image = composed.quantize(colors=54, method=Image.Quantize.MEDIANCUT,
                              dither=Image.Dither.NONE).convert("RGBA")
    grade_patrol_texture(image)
    add_modest_wear(image)
    paint_locked_details(image)
    TEXTURE_DIR.mkdir(parents=True, exist_ok=True)
    image.save(TEXTURE, optimize=True)


def projected_contains(point, triangle):
    a, b, c = triangle
    u = b - a
    v = c - a
    q = np.asarray(point) - a
    determinant = u[0] * v[1] - u[1] * v[0]
    if abs(determinant) < 1e-10:
        return False
    s = (q[0] * v[1] - q[1] * v[0]) / determinant
    t = (u[0] * q[1] - u[1] * q[0]) / determinant
    return s >= -1e-6 and t >= -1e-6 and s + t <= 1 + 1e-6


def read_validated(name):
    vertices, indices = read_mesh(MODEL / (name + ".emesh"))
    if not len(indices) or indices.min() < 0 or indices.max() >= len(vertices):
        raise ValueError(f"{name}: invalid/empty indices")
    if not np.isfinite(vertices).all():
        raise ValueError(f"{name}: non-finite vertex")
    if not ((vertices[:, 6:8] >= 0) & (vertices[:, 6:8] <= 1)).all():
        raise ValueError(f"{name}: UV outside atlas")
    triangles = vertices[indices, :3]
    area = np.linalg.norm(np.cross(triangles[:, 1] - triangles[:, 0],
                                   triangles[:, 2] - triangles[:, 0]), axis=1)
    if not (area > 1e-9).all():
        raise ValueError(f"{name}: degenerate triangle")
    normal_length = np.linalg.norm(vertices[:, 3:6], axis=1)
    if not np.allclose(normal_length, 1, atol=2e-4):
        raise ValueError(f"{name}: invalid normals")
    return vertices, indices, triangles


def assert_capped(name, vertices, indices, ignored_card_planes=()):
    edges = {}
    positions = [tuple(np.round(point, 6)) for point in vertices[:, :3]]
    for triangle in indices:
        xs = vertices[triangle, 0]
        if any(np.isclose(xs, plane, atol=2e-6).all()
               for plane in ignored_card_planes):
            continue
        points = [positions[int(index)] for index in triangle]
        for first, second in ((0, 1), (1, 2), (2, 0)):
            edge = tuple(sorted((points[first], points[second])))
            edges[edge] = edges.get(edge, 0) + 1
    odd = [edge for edge, count in edges.items() if count % 2]
    if odd:
        raise ValueError(f"{name}: uncapped boundary edges {odd[:4]}")


def validate_texture():
    with Image.open(TEXTURE) as source:
        image = source.convert("RGBA")
        if source.mode != "RGBA" or image.size != (ATLAS_SIZE, ATLAS_SIZE):
            raise ValueError(f"atlas must be 256x256 RGBA, got {source.mode} {image.size}")
        pixels = np.asarray(image)
        glass_alpha = []
        glass_boxes = []
        for name in ("GLASS_SIDE", "GLASS_FRONT", "GLASS_REAR"):
            x0, y0, x1, y1 = REGIONS[name]
            glass_alpha.extend(pixels[y0:y1 + 1, x0:x1 + 1, 3].ravel())
            glass_boxes.append((x0, y0, x1, y1))
        if not glass_alpha or not all(120 <= int(value) <= 190 for value in glass_alpha):
            raise ValueError("glass receiver alpha is not semi-transparent")
        opaque = pixels[:, :, 3].copy()
        for x0, y0, x1, y1 in glass_boxes:
            opaque[y0:y1 + 1, x0:x1 + 1] = 255
        if not (opaque == 255).all():
            raise ValueError("non-glass atlas pixels must remain opaque")
        colours = len(set(image.getdata()))
        if colours > 96:
            raise ValueError(f"PSX palette too broad: {colours}")
        for name in ("POLICE_DRIVER", "POLICE_PASSENGER"):
            x0, y0, x1, y1 = REGIONS[name]
            crop = pixels[y0:y1 + 1, x0:x1 + 1, :3]
            navy = (crop[:, :, 2] > crop[:, :, 0] * 1.30) & (crop[:, :, 0] < 60)
            if navy.sum() < 90:
                raise ValueError(f"{name}: exact POLICE marking missing")
        for name, channel in (("LIGHTBAR_RED", 0), ("LIGHTBAR_BLUE", 2)):
            x0, y0, x1, y1 = REGIONS[name]
            crop = pixels[y0:y1 + 1, x0:x1 + 1, :3]
            if crop[:, :, channel].mean() < 90:
                raise ValueError(f"{name}: locked lens colour missing")
    return colours


def validate_steering(triangles):
    wheel = read_part(ROOT / "assets/models/vehicles/common/wheel.emesh",
                      ROOT / "assets/textures/vehicles/common/wheel.png")
    native_radius = max(np.ptp(wheel.positions[:, 1]),
                        np.ptp(wheel.positions[:, 2])) * .5
    half_width = float(np.max(np.abs(wheel.positions[:, 0])) *
                       WHEELS["radius"] / native_radius)
    weights = np.asarray([(i / 16, j / 16, 1 - (i + j) / 16)
                          for i in range(17) for j in range(17 - i)])
    minimum = float("inf")
    probes = 0
    for axle in (WHEELS["front_z"], WHEELS["rear_z"]):
        for side in (-1, 1):
            centre = np.asarray((side * WHEELS["x"], WHEELS["arch_y"], axle))
            near = triangles[
                ((triangles.min(1) - centre) < (.62, .52, .62)).all(1) &
                ((triangles.max(1) - centre) > (-.62, -.52, -.62)).all(1)]
            cloud = np.einsum("ij,tjk->tik", weights, near).reshape(-1, 3) - centre
            angles = np.linspace(-.72, .72, 25) if axle > 0 else [0]
            for angle in angles:
                cosine, sine = math.cos(angle), math.sin(angle)
                local = np.column_stack((cloud[:, 0] * cosine + cloud[:, 2] * sine,
                                         cloud[:, 1],
                                         -cloud[:, 0] * sine + cloud[:, 2] * cosine))
                gap = np.maximum(np.abs(local[:, 0]) - half_width,
                                 np.hypot(local[:, 1], local[:, 2]) - WHEELS["radius"])
                if len(gap):
                    minimum = min(minimum, float(gap.min()))
                    if (gap < .001).any():
                        raise ValueError(
                            f"shared tire/body overlap: axle={axle} side={side} "
                            f"steer={angle:.3f} gap={gap.min():.5f}")
                    probes += len(gap)
    return {"surface_samples": probes, "minimum_sampled_margin": minimum,
            "steer_radians": .72, "wheel_half_width": half_width}


def make_uv_guide(vertices, indices):
    image = Image.open(TEXTURE).convert("RGBA").resize((768, 768),
                                                       Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(image)
    for triangle in indices:
        points = [(float(vertices[index][6]) * 768,
                   (1 - float(vertices[index][7])) * 768)
                  for index in triangle]
        draw.line((*points, points[0]), fill=(55, 255, 199, 220), width=1)
    UV_GUIDE.parent.mkdir(parents=True, exist_ok=True)
    image.save(UV_GUIDE)


def validate(texture_source):
    meshes = {}
    for name in ("body", "body_open", "driver_door", *GLASS_NAMES):
        meshes[name] = read_validated(name)
    vertices, indices, triangles = meshes["body"]
    triangle_count = len(indices)
    if not SHAPE["triangle_budget"][0] <= triangle_count <= SHAPE["triangle_budget"][1]:
        raise ValueError(f"closed body triangle budget missed: {triangle_count}")
    lo, hi = vertices[:, :3].min(0), vertices[:, :3].max(0)
    size = hi - lo
    if not 2.075 <= size[0] <= 2.090 or not 5.295 <= size[2] <= 5.305:
        raise ValueError(f"bad overall footprint: {size}")
    if not np.isclose(lo[1], .20, atol=.002) or not np.isclose(hi[1], 1.77, atol=.002):
        raise ValueError(f"bad vertical bounds: {lo[1]}..{hi[1]}")

    names = ("body_open", "driver_door", *GLASS_NAMES)
    articulated_points = np.concatenate([meshes[name][0][:, :3] for name in names])
    articulated_triangles = sum(len(meshes[name][1]) for name in names)
    if articulated_triangles != triangle_count:
        raise ValueError(("closed/articulated triangle mismatch",
                          triangle_count, articulated_triangles))
    if not np.allclose(articulated_points.min(0), lo, atol=1e-5) or \
            not np.allclose(articulated_points.max(0), hi, atol=1e-5):
        raise ValueError("closed/articulated bounds changed")

    assert_capped("driver_door", meshes["driver_door"][0],
                  meshes["driver_door"][1], (1.019, 1.021, 1.041))
    for name in GLASS_NAMES:
        assert_capped(name, meshes[name][0], meshes[name][1])
    if meshes["driver_glass"][0][:, 0].min() < .70:
        raise ValueError("driver glass is not fitted to the +X moving door")

    body_open = meshes["body_open"][2]
    door = meshes["driver_door"][2]
    outside_body = body_open[(body_open[:, :, 0] > .80).all(1)]
    outside_door = door[(door[:, :, 0] > .80).all(1)]
    doorway_probes = 0
    for forward in np.linspace(-.18, .62, 6):
        for up in (.50, .70, .88):
            point = np.asarray((forward, up))
            if not any(projected_contains(point, tri[:, [2, 1]])
                       for tri in outside_door):
                raise ValueError(f"driver door skin misses {point}")
            if any(projected_contains(point, tri[:, [2, 1]])
                   for tri in outside_body):
                raise ValueError(f"stationary body blocks driver doorway {point}")
            doorway_probes += 1
    for forward in (-.10, .15, .40):
        for up in (1.16, 1.34, 1.43):
            point = np.asarray((forward, up))
            if any(projected_contains(point, tri[:, [2, 1]])
                   for tri in outside_body):
                raise ValueError(f"opaque shell behind driver glass {point}")

    opening_probes = 0
    for axle in (WHEELS["front_z"], WHEELS["rear_z"]):
        for side in (-1, 1):
            side_triangles = triangles[(side * triangles[:, :, 0] > .84).all(1)]
            for dz in (-.18, 0, .18):
                for dy in (-.12, 0, .12):
                    point = np.asarray((axle + dz, WHEELS["arch_y"] + dy))
                    if any(projected_contains(point, tri[:, [2, 1]])
                           for tri in side_triangles):
                        raise ValueError(f"closed wheel opening {side} {point}")
                    opening_probes += 1
        for x in (-.55, 0, .55):
            for forward in (axle - .06, axle + .06):
                if not any(projected_contains((x, forward), tri[:, [0, 2]])
                           for tri in triangles[(triangles[:, :, 1] > .76).all(1)]):
                    raise ValueError(f"missing centre hood/deck at {(x, forward)}")

    steering = validate_steering(triangles)
    colours = validate_texture()
    make_uv_guide(vertices, indices)

    per_part = {
        name: {
            "vertices": len(meshes[name][0]),
            "triangles": len(meshes[name][1]),
            "bounds_min": [round(float(value), 5)
                           for value in meshes[name][0][:, :3].min(0)],
            "bounds_max": [round(float(value), 5)
                           for value in meshes[name][0][:, :3].max(0)],
        }
        for name in names
    }
    files = [MODEL / "body.emesh", MODEL / "body_open.emesh",
             MODEL / "driver_door.emesh",
             *(MODEL / (name + ".emesh") for name in GLASS_NAMES),
             TEXTURE, IMAGEGEN_SOURCE, PROMPT]
    report = {
        "asset": SHAPE["name"],
        "attempt": "B transitional aero",
        "mesh": str(MODEL / "body.emesh"),
        "texture": str(TEXTURE),
        "imagegen_source": str(IMAGEGEN_SOURCE),
        "imagegen_prompt": str(PROMPT),
        "imagegen_template": str(IMAGEGEN_TEMPLATE),
        "texture_source": texture_source,
        "vertices": len(vertices),
        "triangles": triangle_count,
        "bounds_min": [round(float(value), 5) for value in lo],
        "bounds_max": [round(float(value), 5) for value in hi],
        "bounds_size": [round(float(value), 5) for value in size],
        "body_roof_height": SHAPE["roof_height"],
        "lightbar_height": SHAPE["lightbar_height"],
        "shape_contract": SHAPE,
        "wheel_anchors": WHEELS,
        "door": DOOR,
        "driver": DRIVER,
        "headlights": HEADLIGHTS,
        "brakelights": BRAKELIGHTS,
        "lightbar": LIGHTBAR,
        "articulated_parts": per_part,
        "atlas": [256, 256, "RGBA"],
        "palette_colours": colours,
        "glass_alpha": 150,
        "uv_contract": "final closed mesh -> exact 4x receiver template -> built-in imagegen edit -> deterministic 256 reduction",
        "wheel_opening_probes": opening_probes,
        "doorway_probes": doorway_probes,
        "steering": steering,
        "checks": [
            "one joined wheel-less closed body",
            "body_open plus separate capped driver door",
            "six named capped semi-transparent glass panes",
            "driver_glass located on and rotates with the +X driver door",
            "hollow cabin with floor, seats, dash, steering and partition",
            "four real open wheel wells and retained centre hood/deck",
            "actual shared wheel sampled through +/-0.72 rad steering",
            "exact POLICE text plus critical lamp/lightbar cells locked after reduction",
            "256x256 RGBA limited-palette atlas",
        ],
        "sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in files if path.is_file()
        },
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({
        "triangles": report["triangles"],
        "bounds_size": report["bounds_size"],
        "palette_colours": colours,
        "articulated_triangles": articulated_triangles,
        "steering_minimum_margin": steering["minimum_sampled_margin"],
    }, indent=2))
    return report


def rotate_part(part, hinge, degrees):
    angle = math.radians(degrees)
    cosine, sine = math.cos(angle), math.sin(angle)
    rotation = np.asarray(((cosine, 0, sine), (0, 1, 0),
                           (-sine, 0, cosine)))
    return Part((part.positions - hinge) @ rotation.T + hinge,
                part.normals @ rotation.T, part.uvs, part.indices, part.texture)


def shared_wheels(steer=0):
    wheel = read_part(ROOT / "assets/models/vehicles/common/wheel.emesh",
                      ROOT / "assets/textures/vehicles/common/wheel.png")
    native_radius = max(np.ptp(wheel.positions[:, 1]),
                        np.ptp(wheel.positions[:, 2])) * .5
    result = []
    for axle in (WHEELS["front_z"], WHEELS["rear_z"]):
        for side in (-1, 1):
            angle = steer if axle > 0 else 0
            cosine, sine = math.cos(angle), math.sin(angle)
            rotation = np.asarray(((cosine, 0, -sine), (0, 1, 0),
                                   (sine, 0, cosine)))
            positions = wheel.positions * (WHEELS["radius"] / native_radius)
            positions = positions @ rotation.T
            positions += np.asarray((side * WHEELS["x"],
                                     WHEELS["arch_y"], axle))
            result.append(Part(positions, wheel.normals @ rotation.T,
                               wheel.uvs, wheel.indices, wheel.texture))
    return result


def closed_parts(steer=0):
    return [read_part(MODEL / "body.emesh", TEXTURE), *shared_wheels(steer)]


def articulated_parts(opened):
    body = read_part(MODEL / "body_open.emesh", TEXTURE)
    door = read_part(MODEL / "driver_door.emesh", TEXTURE)
    panes = {name: read_part(MODEL / (name + ".emesh"), TEXTURE)
             for name in GLASS_NAMES}
    if opened:
        hinge = np.asarray(DOOR["hinge"])
        door = rotate_part(door, hinge, DOOR["open_degrees"])
        panes["driver_glass"] = rotate_part(panes["driver_glass"], hinge,
                                             DOOR["open_degrees"])
    return [body, door, *(panes[name] for name in GLASS_NAMES), *shared_wheels()]


def make_previews(blockout=False):
    straight = closed_parts()
    locked = closed_parts(.72)
    views = [
        ("FRONT 3/4", straight, -32, 18), ("STRICT SIDE", straight, -90, 2),
        ("REAR 3/4", straight, -148, 18),
        ("ELEVATED FRONT", straight, -32, 38),
        ("SHARED WHEELS FULL LOCK", locked, -32, 25),
        ("PASSENGER SIDE", straight, 90, 4),
    ]
    sheet = Image.new("RGB", (1440, 840), (19, 21, 25))
    draw = ImageDraw.Draw(sheet)
    for index, (label, parts, yaw, pitch) in enumerate(views):
        x, y = (index % 3) * 480, (index // 3) * 420
        with np.errstate(all="ignore"):
            shot = raster_view(parts, yaw, pitch, 480, 386)
        sheet.paste(shot, (x, y))
        draw.text((x + 12, y + 397), label, fill=(230, 230, 218))
    sheet.save(BLOCKOUT_PREVIEW if blockout else PREVIEW)

    if not blockout:
        closed = articulated_parts(False)
        opened = articulated_parts(True)
        views = [
            ("CLOSED ARTICULATED ASSEMBLY", closed, -32, 18),
            ("OPEN +X DRIVER DOOR + GLASS", opened, -32, 18),
            ("HOLLOW CAB / FLOOR / SEATS", opened, -70, 30),
            ("OPEN DOOR REAR 3/4", opened, -138, 22),
        ]
        sheet = Image.new("RGB", (1200, 840), (19, 21, 25))
        draw = ImageDraw.Draw(sheet)
        for index, (label, parts, yaw, pitch) in enumerate(views):
            x, y = (index % 2) * 600, (index // 2) * 420
            with np.errstate(all="ignore"):
                shot = raster_view(parts, yaw, pitch, 600, 386)
            sheet.paste(shot, (x, y))
            draw.text((x + 12, y + 397), label, fill=(230, 230, 218))
        sheet.save(OPEN_PREVIEW)


def build_model():
    MODEL.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        str(BLENDER), "--background", "--factory-startup", "--python",
        str(ROOT / "tools/municipal_cruiser_91b_blender.py"), "--",
        "--mesh", str(MODEL / "body.emesh"),
        "--blend", str(MODEL / "source.blend"),
    ], cwd=ROOT, check=True)


def run_asset_lab():
    screenshot = ROOT / "build/municipal-cruiser-91b-engine.png"
    log = ROOT / "build/municipal-cruiser-91b-engine.log"
    process = subprocess.run([
        str(ROOT / "build/bin/apricot_asset_lab"),
        "--model", str(MODEL / "body.emesh"),
        "--texture", str(TEXTURE), "--yaw", "212", "--frames", "90",
        "--screenshot", str(screenshot),
    ], cwd=ROOT, capture_output=True, text=True)
    log.write_text(process.stdout + process.stderr)
    process.check_returncode()
    if "0 GL errors" not in process.stdout:
        raise RuntimeError(process.stdout + process.stderr)
    print("asset-lab 90 frames, 0 GL errors")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare-imagegen", action="store_true")
    parser.add_argument("--texture-only", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--preview-only", action="store_true")
    parser.add_argument("--asset-lab", action="store_true")
    args = parser.parse_args()
    (ROOT / "build").mkdir(exist_ok=True)
    MODEL.mkdir(parents=True, exist_ok=True)
    TEXTURE_DIR.mkdir(parents=True, exist_ok=True)

    if not (args.texture_only or args.validate_only or args.preview_only):
        if args.prepare_imagegen or not TEXTURE.is_file():
            make_blockout_texture()
        build_model()

    vertices, indices = read_mesh(MODEL / "body.emesh")
    make_imagegen_template(vertices, indices)
    if args.prepare_imagegen:
        make_blockout_texture()
        texture_source = "model-derived semantic blockout before built-in imagegen edit"
    elif args.texture_only or not (args.validate_only or args.preview_only):
        cook_imagegen_texture()
        texture_source = "built-in imagegen edit of exact final 4x UV/component template"
    else:
        texture_source = "existing cooked texture"
    validate(texture_source)
    make_previews(args.prepare_imagegen)
    if args.asset_lab:
        run_asset_lab()


if __name__ == "__main__":
    main()
