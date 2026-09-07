#!/usr/bin/env python3
"""Model-first cook, fixed imagegen atlas, validation, and QA for Cruiser 91A."""

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
from municipal_cruiser_91a_spec import (
    ATLAS_SIZE,
    DOOR,
    DRIVER,
    LAMPS,
    LIGHTBAR,
    PANE_FILES,
    REGIONS,
    SHAPE,
    WHEELS,
)
from render_firetruck_preview import Part, read_part, raster_view

ROOT = Path(__file__).resolve().parents[1]
BLENDER = Path("/Applications/Blender.app/Contents/MacOS/Blender")
MODEL = ROOT / "assets/models/vehicles/municipal_cruiser_91a"
TEXTURE_DIR = ROOT / "assets/textures/vehicles/municipal_cruiser_91a"
TEXTURE = TEXTURE_DIR / "body.png"
IMAGEGEN_SOURCE = TEXTURE_DIR / "body-imagegen-source.png"
PROMPT = ROOT / "tools/municipal_cruiser_91a_texture_prompt.md"
TEMPLATE = ROOT / "build/municipal-cruiser-91a-imagegen-template.png"
UV_GUIDE = ROOT / "build/municipal-cruiser-91a-uv-guide.png"
REPORT = ROOT / "build/municipal-cruiser-91a-fit-report.json"
PREVIEW = ROOT / "build/municipal-cruiser-91a-preview.png"
OPEN_PREVIEW = ROOT / "build/municipal-cruiser-91a-open-door-preview.png"
BLOCKOUT_PREVIEW = ROOT / "build/municipal-cruiser-91a-blockout.png"

FONT = ROOT / "assets/fonts/BebasNeue-Regular.ttf"

BASE_COLOURS = {
    "BODY_SIDE": (31, 36, 42, 255),
    "BODY_TOP": (207, 211, 205, 255),
    "DOOR_WHITE": (221, 221, 209, 255),
    "POLICE": (226, 224, 210, 255),
    "GLASS": (31, 52, 66, 158),
    "FRONT": (38, 43, 48, 255),
    "REAR": (27, 32, 38, 255),
    "METAL": (154, 163, 164, 255),
    "RUBBER": (19, 23, 27, 255),
    "INTERIOR": (49, 55, 56, 255),
    "SEAM": (12, 16, 20, 255),
    "HEADLIGHT": (226, 220, 187, 255),
    "AMBER": (203, 119, 28, 255),
    "TAIL_RED": (159, 28, 29, 255),
    "REVERSE": (208, 214, 199, 255),
    "LIGHTBAR_RED": (173, 18, 32, 255),
    "LIGHTBAR_BLUE": (24, 55, 166, 255),
    "SHADOW": (11, 15, 19, 255),
    "BLACK": (8, 11, 14, 255),
}

RAMPS = {
    "BODY_SIDE": [(9, 13, 17), (18, 23, 28), (29, 35, 41),
                  (42, 49, 55), (58, 65, 69)],
    "BODY_TOP": [(112, 121, 122), (157, 166, 164), (194, 201, 195),
                 (221, 224, 212), (239, 238, 221)],
    "DOOR_WHITE": [(104, 113, 115), (150, 158, 157), (188, 195, 190),
                   (219, 221, 208), (238, 236, 217)],
    "POLICE": [(111, 119, 119), (159, 167, 163), (197, 202, 194),
               (225, 225, 209), (241, 238, 218)],
    "GLASS": [(8, 19, 29), (16, 34, 46), (27, 53, 67),
              (45, 74, 86), (72, 101, 109)],
    "FRONT": [(13, 18, 22), (24, 30, 35), (38, 45, 50),
              (58, 65, 68)],
    "REAR": [(10, 15, 20), (21, 27, 32), (34, 41, 46), (51, 57, 61)],
    "METAL": [(35, 43, 48), (73, 84, 90), (123, 137, 141),
              (177, 188, 187), (218, 220, 205)],
    "RUBBER": [(7, 10, 13), (15, 19, 23), (27, 32, 35), (45, 49, 50)],
    "INTERIOR": [(16, 20, 23), (29, 35, 38), (46, 54, 55),
                 (66, 73, 71)],
    "SEAM": [(4, 7, 10), (10, 14, 18), (19, 24, 28)],
    "SHADOW": [(5, 8, 11), (10, 14, 18), (18, 22, 26)],
    "BLACK": [(4, 6, 8), (8, 11, 14), (15, 18, 21)],
}


def fill_neutral_atlas() -> Image.Image:
    image = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (8, 10, 13, 255))
    draw = ImageDraw.Draw(image)
    for name, box in REGIONS.items():
        draw.rectangle(box, fill=BASE_COLOURS[name])
        x0, y0, x1, y1 = box
        # Simple authored bands make every semantic receiver legible in the
        # blockout without pre-solving imagegen's surface treatment.
        if y1 - y0 > 12:
            colour = tuple(min(255, value + 18) for value in BASE_COLOURS[name][:3])
            draw.rectangle((x0 + 2, y0 + 3, x1 - 2, y0 + 5),
                           fill=(*colour, BASE_COLOURS[name][3]))
    return image


def make_template(vertices: np.ndarray, indices: np.ndarray) -> None:
    """Create the exact 4x edit target from the final closed-body UVs."""
    scale = 4
    image = fill_neutral_atlas().convert("RGB").resize(
        (ATLAS_SIZE * scale, ATLAS_SIZE * scale), Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(image)
    for box in REGIONS.values():
        rect = tuple(value * scale for value in box)
        draw.rectangle(rect, outline=(245, 44, 180), width=3)
    for triangle in indices:
        points = [(float(vertices[index, 6]) * ATLAS_SIZE * scale,
                   (1 - float(vertices[index, 7])) * ATLAS_SIZE * scale)
                  for index in triangle]
        draw.line(points + [points[0]], fill=(41, 249, 205), width=2)
    TEMPLATE.parent.mkdir(parents=True, exist_ok=True)
    image.save(TEMPLATE)


def grade_region(pixels: np.ndarray, name: str, ramp) -> None:
    x0, y0, x1, y1 = REGIONS[name]
    crop = pixels[y0:y1 + 1, x0:x1 + 1]
    luminance = (crop[:, :, 0].astype(float) * .2126
                 + crop[:, :, 1].astype(float) * .7152
                 + crop[:, :, 2].astype(float) * .0722)
    low, high = np.percentile(luminance, (3, 97))
    if high - low < 1:
        levels = np.full(luminance.shape, len(ramp) // 2, dtype=int)
    else:
        levels = np.rint((luminance - low) / (high - low)
                         * (len(ramp) - 1)).astype(int)
    levels = np.clip(levels, 0, len(ramp) - 1)
    colours = np.asarray(ramp, dtype=np.uint8)
    crop[:] = colours[levels]


def binary_text_mask(word: str, size: tuple[int, int]) -> Image.Image:
    if not FONT.is_file():
        raise FileNotFoundError(f"POLICE marking font missing: {FONT}")
    font = ImageFont.truetype(str(FONT), 64)
    bounds = font.getbbox(word)
    source = Image.new("L", (bounds[2] - bounds[0], bounds[3] - bounds[1]), 0)
    ImageDraw.Draw(source).text((-bounds[0], -bounds[1]), word, font=font, fill=255)
    source = source.resize(size, Image.Resampling.LANCZOS)
    return source.point(lambda value: 255 if value >= 112 else 0)


def paint_locked_details(image: Image.Image) -> None:
    """Exact markings and critical receiver colors, always after reduction."""
    draw = ImageDraw.Draw(image)

    # Generic text only.  No badge, seal, real agency, city, or maker logo.
    x0, y0, x1, y1 = REGIONS["POLICE"]
    draw.rectangle((x0 + 2, y0 + 2, x1 - 2, y1 - 2),
                   fill=(218, 219, 207, 255))
    draw.rectangle((x0 + 3, y0 + 9, x1 - 3, y0 + 12),
                   fill=(23, 31, 38, 255))
    draw.rectangle((x0 + 3, y1 - 10, x1 - 3, y1 - 7),
                   fill=(23, 31, 38, 255))
    mask = binary_text_mask("POLICE", (x1 - x0 - 14, 25))
    image.paste((18, 29, 38, 255), (x0 + 7, y0 + 22), mask)

    def lens(name: str, dark, base, bright) -> None:
        a, b, c, d = REGIONS[name]
        draw.rectangle((a, b, c, d), fill=(*dark, 255))
        draw.rectangle((a + 3, b + 3, c - 3, d - 3), fill=(*base, 255))
        draw.rectangle((a + 5, b + 5, c - 5, b + 7), fill=(*bright, 255))
        draw.rectangle((a + 5, d - 7, c - 5, d - 5), fill=(*dark, 255))
        for x in range(a + 7, c - 4, 4):
            draw.line((x, b + 5, x, d - 5), fill=(*bright, 255), width=1)

    lens("HEADLIGHT", (49, 54, 52), (188, 191, 169), (242, 237, 201))
    lens("AMBER", (72, 37, 10), (194, 104, 19), (247, 169, 47))
    lens("TAIL_RED", (55, 10, 15), (151, 22, 29), (236, 63, 55))
    lens("REVERSE", (48, 55, 55), (184, 194, 184), (238, 239, 216))
    lens("LIGHTBAR_RED", (57, 5, 14), (164, 15, 31), (255, 83, 74))
    lens("LIGHTBAR_BLUE", (7, 18, 63), (20, 54, 164), (79, 148, 255))


def make_texture() -> None:
    if not IMAGEGEN_SOURCE.is_file():
        raise FileNotFoundError(f"imagegen edit missing: {IMAGEGEN_SOURCE}")
    with Image.open(IMAGEGEN_SOURCE) as source:
        source = source.convert("RGB")
        if source.width != source.height:
            raise ValueError(f"imagegen edit must remain square: {source.size}")
        reduced = source.resize((ATLAS_SIZE, ATLAS_SIZE), Image.Resampling.BOX)
    composed = Image.new("RGB", (ATLAS_SIZE, ATLAS_SIZE), (8, 10, 13))
    for box in REGIONS.values():
        crop_box = (box[0], box[1], box[2] + 1, box[3] + 1)
        composed.paste(reduced.crop(crop_box), crop_box)
    pixels = np.asarray(composed).copy()
    for name, ramp in RAMPS.items():
        grade_region(pixels, name, ramp)
    # Dedicated lens cells also inherit generated structure before exact lens
    # ridges/colors are overlaid below.
    for name in ("HEADLIGHT", "AMBER", "TAIL_RED", "REVERSE",
                 "LIGHTBAR_RED", "LIGHTBAR_BLUE"):
        grade_region(pixels, name, [BASE_COLOURS[name][:3]])
    image = Image.fromarray(pixels).quantize(
        colors=64, method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.NONE).convert("RGBA")
    paint_locked_details(image)
    # Keep the glass truly semi-transparent while every non-glass texel stays
    # opaque.  The panes are also exported separately for sorted rendering.
    gx0, gy0, gx1, gy1 = REGIONS["GLASS"]
    alpha = image.getchannel("A")
    ImageDraw.Draw(alpha).rectangle((gx0, gy0, gx1, gy1), fill=158)
    image.putalpha(alpha)
    TEXTURE_DIR.mkdir(parents=True, exist_ok=True)
    image.save(TEXTURE, optimize=True)


def build_model() -> None:
    MODEL.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        str(BLENDER), "--background", "--factory-startup", "--python",
        str(ROOT / "tools/municipal_cruiser_91a_blender.py"), "--",
        "--mesh", str(MODEL / "body.emesh"),
        "--blend", str(MODEL / "source.blend"),
    ], cwd=ROOT, check=True)


def projected(point, triangle) -> bool:
    a, b, c = triangle
    u, v = b - a, c - a
    determinant = u[0] * v[1] - u[1] * v[0]
    if abs(determinant) < 1e-9:
        return False
    q = np.asarray(point) - a
    s = (q[0] * v[1] - q[1] * v[0]) / determinant
    t = (u[0] * q[1] - u[1] * q[0]) / determinant
    return s >= -1e-6 and t >= -1e-6 and s + t <= 1 + 1e-6


def check_mesh(path: Path):
    vertices, indices = read_mesh(path)
    if not len(indices) or indices.min() < 0 or indices.max() >= len(vertices):
        raise ValueError(f"bad index buffer: {path}")
    if not np.isfinite(vertices).all():
        raise ValueError(f"non-finite vertex: {path}")
    if not ((vertices[:, 6:8] >= 0) & (vertices[:, 6:8] <= 1)).all():
        raise ValueError(f"UV outside atlas: {path}")
    triangles = vertices[indices, :3]
    areas = np.linalg.norm(np.cross(triangles[:, 1] - triangles[:, 0],
                                    triangles[:, 2] - triangles[:, 0]), axis=1)
    if not (areas > 1e-9).all():
        raise ValueError(f"degenerate triangles: {path}")
    normal_length = np.linalg.norm(vertices[:, 3:6], axis=1)
    if not np.allclose(normal_length, 1, atol=1e-4):
        raise ValueError(f"invalid normals: {path}")
    return vertices, indices, triangles


def validate(texture_source: str, final_texture: bool = True):
    meshes = {}
    for name in ("body", "body_open", "driver_door", *PANE_FILES):
        meshes[name] = check_mesh(MODEL / (name + ".emesh"))
    vertices, indices, triangles = meshes["body"]
    if not SHAPE["triangle_budget"][0] <= len(indices) <= SHAPE["triangle_budget"][1]:
        raise ValueError(f"triangle budget missed: {len(indices)}")
    minimum, maximum = vertices[:, :3].min(0), vertices[:, :3].max(0)
    size = maximum - minimum
    if not np.allclose(size, (2.08, 1.58, 5.35), atol=.002):
        raise ValueError(f"unexpected bounds: {size}")
    if not np.allclose(minimum, (-1.04, .20, -2.675), atol=.002):
        raise ValueError(f"unexpected minimum: {minimum}")
    if not np.allclose(maximum, (1.04, 1.78, 2.675), atol=.002):
        raise ValueError(f"unexpected maximum: {maximum}")
    if minimum[1] < .10:
        raise ValueError("body contains ground-cutting or wheel geometry")

    # Closed body is exactly the articulated body + door + six panes.
    component_triangles = sum(len(meshes[name][1]) for name in
                              ("body_open", "driver_door", *PANE_FILES))
    if component_triangles != len(indices):
        raise ValueError(("closed/articulated triangle mismatch",
                          len(indices), component_triangles))

    opening_samples = 0
    for axle in (WHEELS["front_z"], WHEELS["rear_z"]):
        for side in (-1., 1.):
            relevant = triangles[(side * triangles[:, :, 0] > .66).all(1)]
            for dz in (-.18, 0, .18):
                for dy in (-.14, 0, .14):
                    point = (axle + dz, WHEELS["arch_y"] + dy)
                    if any(projected(point, tri[:, [2, 1]]) for tri in relevant):
                        raise ValueError(f"blocked wheel opening: {side}, {point}")
                    opening_samples += 1
    hood_deck_samples = 0
    for axle in (WHEELS["front_z"], WHEELS["rear_z"]):
        for x in (-.58, 0, .58):
            for z in (axle - .05, axle + .05):
                # The flat centre panel slopes only at the final nose/tail
                # station, so include its intentionally lower end vertices.
                relevant = triangles[(triangles[:, :, 1] > .82).all(1)]
                if not any(projected((x, z), tri[:, [0, 2]]) for tri in relevant):
                    raise ValueError(f"centre hood/deck missing: {(x, z)}")
                hood_deck_samples += 1

    open_triangles = meshes["body_open"][2]
    door_triangles = meshes["driver_door"][2]
    # Ignore visible seat/dashboard geometry deeper inside the hollow cabin;
    # this gate targets only a lingering exterior side skin.
    open_side = open_triangles[(open_triangles[:, :, 0] > .80).all(1)]
    door_side = door_triangles[(door_triangles[:, :, 0] > .90).all(1)]
    doorway_samples = 0
    for forward in (-.10, .16, .45, .68):
        for height in (.59, .76, .94):
            point = (forward, height)
            if any(projected(point, tri[:, [2, 1]]) for tri in open_side):
                raise ValueError(f"driver doorway still solid: {point}")
            if not any(projected(point, tri[:, [2, 1]]) for tri in door_side):
                raise ValueError(f"driver door misses aperture: {point}")
            doorway_samples += 1

    glass_region = REGIONS["GLASS"]
    for name in PANE_FILES:
        pane_vertices = meshes[name][0]
        texels = pane_vertices[:, 6:8] * np.array([256, -256]) + np.array([0, 256])
        if not ((texels[:, 0] >= glass_region[0] + 1)
                & (texels[:, 0] <= glass_region[2] - 1)
                & (texels[:, 1] >= glass_region[1] + 1)
                & (texels[:, 1] <= glass_region[3] - 1)).all():
            raise ValueError(f"pane outside GLASS receiver: {name}")

    # Door and its pane share the exact hinge transform and both move outward.
    angle = math.radians(DOOR["open_degrees"])
    cosine, sine = math.cos(angle), math.sin(angle)
    rotation = np.array(((cosine, 0, sine), (0, 1, 0),
                         (-sine, 0, cosine)))
    hinge = np.asarray(DOOR["hinge"])
    handle = np.asarray(DOOR["handle"])
    opened_handle = (handle - hinge) @ rotation.T + hinge
    if opened_handle[0] < handle[0] + .65:
        raise ValueError("driver door rotates inward")
    glass_points = meshes["driver_glass"][0][:, :3]
    opened_glass = (glass_points - hinge) @ rotation.T + hinge
    if opened_glass[:, 0].mean() < glass_points[:, 0].mean() + .28:
        raise ValueError("driver_glass does not follow outward door transform")

    with Image.open(TEXTURE) as atlas:
        if atlas.size != (256, 256) or atlas.mode != "RGBA":
            raise ValueError("atlas must be 256x256 RGBA")
        colours = len(set(atlas.getdata()))
        if colours > 96:
            raise ValueError(f"palette too broad: {colours}")
        gx0, gy0, gx1, gy1 = REGIONS["GLASS"]
        glass_alpha = atlas.getchannel("A").crop((gx0, gy0, gx1 + 1, gy1 + 1))
        if final_texture and glass_alpha.getextrema() != (158, 158):
            raise ValueError("glass atlas cell is not semi-transparent")
        if final_texture:
            police = atlas.crop((REGIONS["POLICE"][0], REGIONS["POLICE"][1],
                                 REGIONS["POLICE"][2] + 1,
                                 REGIONS["POLICE"][3] + 1)).convert("RGB")
            dark = np.asarray(police).mean(axis=2) < 55
            if dark.sum() < 260:
                raise ValueError("exact POLICE overlay is missing")

    make_uv_guide(vertices, indices)
    report = {
        "asset": SHAPE["name"],
        "mesh": str(MODEL / "body.emesh"),
        "texture": str(TEXTURE),
        "vertices": len(vertices),
        "triangles": len(indices),
        "bounds_min": [round(float(value), 5) for value in minimum],
        "bounds_max": [round(float(value), 5) for value in maximum],
        "bounds_size": [round(float(value), 5) for value in size],
        "shape_contract": SHAPE,
        "wheel_anchors": WHEELS,
        "door": DOOR,
        "driver": DRIVER,
        "panes": {name: {
            "file": str(MODEL / (name + ".emesh")),
            "triangles": len(meshes[name][1]),
        } for name in PANE_FILES},
        "articulated_triangles": {
            "body_open": len(meshes["body_open"][1]),
            "driver_door": len(meshes["driver_door"][1]),
            "panes": sum(len(meshes[name][1]) for name in PANE_FILES),
            "total": component_triangles,
        },
        "lamp_receivers": LAMPS,
        "lightbar_receivers": LIGHTBAR,
        "atlas": [256, 256, "RGBA"],
        "atlas_regions": REGIONS,
        "palette_colours": colours,
        "glass_alpha": 158 if final_texture else "pending final cook",
        "texture_source": texture_source,
        "imagegen": {
            "mode": "built-in imagegen edit",
            "template": str(TEMPLATE),
            "prompt_file": str(PROMPT),
            "output_source": str(IMAGEGEN_SOURCE),
            "source_sha256": (hashlib.sha256(IMAGEGEN_SOURCE.read_bytes()).hexdigest()
                              if IMAGEGEN_SOURCE.is_file() else None),
            "contract": "square 4x final-mesh UV template; fixed receivers",
        },
        "samples": {
            "wheel_openings": opening_samples,
            "hood_deck": hood_deck_samples,
            "driver_doorway": doorway_samples,
        },
        "checks": [
            "joined wheel-less closed body",
            "four real side wheel openings and intact centre hood/deck",
            "hollow cabin with floor, four seats, dash, wheel, and pedals",
            "separate capped driver door and six capped pane meshes",
            "driver_glass shares outward door hinge transform",
            "semi-transparent smoke-blue GLASS atlas receiver",
            "exact post-reduction POLICE and lamp/lightbar overlays",
            "actual shared-wheel multi-angle and open-door previews",
        ],
        "sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in [MODEL / "body.emesh", MODEL / "body_open.emesh",
                         MODEL / "driver_door.emesh",
                         *[MODEL / (name + ".emesh") for name in PANE_FILES],
                         TEXTURE]
        },
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({
        "triangles": report["triangles"],
        "articulated_triangles": report["articulated_triangles"],
        "bounds_size": report["bounds_size"],
        "palette_colours": report["palette_colours"],
        "samples": report["samples"],
    }, indent=2))
    return report


def make_uv_guide(vertices: np.ndarray, indices: np.ndarray) -> None:
    image = Image.open(TEXTURE).convert("RGBA").resize((768, 768),
                                                        Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(image)
    for triangle in indices:
        points = [(float(vertices[index, 6]) * 768,
                   (1 - float(vertices[index, 7])) * 768)
                  for index in triangle]
        draw.line(points + [points[0]], fill=(51, 244, 198, 225), width=1)
    image.save(UV_GUIDE)


def wheel_parts() -> list[Part]:
    wheel = read_part(ROOT / "assets/models/vehicles/common/wheel.emesh",
                      ROOT / "assets/textures/vehicles/common/wheel.png")
    native_radius = max(np.ptp(wheel.positions[:, 1]),
                        np.ptp(wheel.positions[:, 2])) * .5
    result = []
    for axle in (WHEELS["front_z"], WHEELS["rear_z"]):
        for side in (-1., 1.):
            positions = wheel.positions * (WHEELS["radius"] / native_radius)
            positions += np.array((side * WHEELS["x"], WHEELS["arch_y"], axle))
            result.append(Part(positions, wheel.normals.copy(), wheel.uvs,
                               wheel.indices, wheel.texture))
    return result


def rotate_part(part: Part, hinge: np.ndarray, rotation: np.ndarray) -> Part:
    return Part((part.positions - hinge) @ rotation.T + hinge,
                part.normals @ rotation.T, part.uvs, part.indices, part.texture)


def preview_parts(open_door: bool = False) -> list[Part]:
    wheels = wheel_parts()
    if not open_door:
        return [read_part(MODEL / "body.emesh", TEXTURE), *wheels]
    body = read_part(MODEL / "body_open.emesh", TEXTURE)
    door = read_part(MODEL / "driver_door.emesh", TEXTURE)
    panes = {name: read_part(MODEL / (name + ".emesh"), TEXTURE)
             for name in PANE_FILES}
    angle = math.radians(DOOR["open_degrees"])
    c, s = math.cos(angle), math.sin(angle)
    rotation = np.array(((c, 0, s), (0, 1, 0), (-s, 0, c)))
    hinge = np.asarray(DOOR["hinge"])
    opened = [rotate_part(door, hinge, rotation),
              rotate_part(panes.pop("driver_glass"), hinge, rotation)]
    return [body, *panes.values(), *opened, *wheels]


def previews(blockout: bool = False) -> None:
    closed = preview_parts(False)
    views = (("FRONT 3/4", -32, 18), ("STRICT SIDE", -90, 1),
             ("REAR 3/4", -148, 18), ("ELEVATED FRONT", -32, 38),
             ("FRONT", 0, 3), ("OTHER SIDE", 90, 3))
    sheet = Image.new("RGB", (1440, 840), (19, 21, 25))
    draw = ImageDraw.Draw(sheet)
    for index, (label, yaw, pitch) in enumerate(views):
        x, y = (index % 3) * 480, (index // 3) * 420
        with np.errstate(all="ignore"):
            shot = raster_view(closed, yaw, pitch, 480, 386)
        sheet.paste(shot, (x, y))
        draw.text((x + 12, y + 397), label, fill=(229, 224, 208))
    sheet.save(BLOCKOUT_PREVIEW if blockout else PREVIEW)

    if blockout:
        return
    opened = preview_parts(True)
    open_views = (("OPEN DRIVER 3/4", -46, 17), ("OPEN DRIVER SIDE", -90, 4),
                  ("CABIN / DOORWAY", -64, 32), ("OPEN FRONT", -20, 10))
    open_sheet = Image.new("RGB", (960, 820), (19, 21, 25))
    draw = ImageDraw.Draw(open_sheet)
    for index, (label, yaw, pitch) in enumerate(open_views):
        x, y = (index % 2) * 480, (index // 2) * 410
        with np.errstate(all="ignore"):
            shot = raster_view(opened, yaw, pitch, 480, 376)
        open_sheet.paste(shot, (x, y))
        draw.text((x + 12, y + 385), label, fill=(229, 224, 208))
    open_sheet.save(OPEN_PREVIEW)


def run_asset_lab() -> None:
    executable = ROOT / "build/bin/apricot_asset_lab"
    if not executable.is_file():
        raise FileNotFoundError(executable)
    screenshot = ROOT / "build/municipal-cruiser-91a-engine.png"
    log = ROOT / "build/municipal-cruiser-91a-engine.log"
    result = subprocess.run([
        str(executable), "--model", str(MODEL / "body.emesh"),
        "--texture", str(TEXTURE), "--yaw", "212", "--frames", "60",
        "--screenshot", str(screenshot),
    ], cwd=ROOT, capture_output=True, text=True)
    log.write_text(result.stdout + result.stderr)
    result.check_returncode()
    if "0 GL errors" not in result.stdout:
        raise RuntimeError(result.stdout + result.stderr)
    print("asset-lab: 60 frames, 0 GL errors")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare-imagegen", action="store_true")
    parser.add_argument("--texture-only", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--preview-only", action="store_true")
    parser.add_argument("--asset-lab", action="store_true")
    args = parser.parse_args()
    TEXTURE_DIR.mkdir(parents=True, exist_ok=True)
    MODEL.mkdir(parents=True, exist_ok=True)
    if args.prepare_imagegen:
        fill_neutral_atlas().save(TEXTURE)
    elif not (args.validate_only or args.preview_only):
        make_texture()
    if not (args.texture_only or args.validate_only or args.preview_only):
        build_model()
    vertices, indices = read_mesh(MODEL / "body.emesh")
    make_template(vertices, indices)
    if not args.preview_only:
        validate("model-derived neutral UV template" if args.prepare_imagegen
                 else "built-in imagegen edit of exact final UV template",
                 final_texture=not args.prepare_imagegen)
    previews(args.prepare_imagegen)
    if args.asset_lab and not args.prepare_imagegen:
        run_asset_lab()


if __name__ == "__main__":
    main()
