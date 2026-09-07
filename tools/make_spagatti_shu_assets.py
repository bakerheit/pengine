#!/usr/bin/env python3
"""Model-first cook, imagegen UV template, validation and QA for Spagatti Shū."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from bake_vehicle_surfaces import read_mesh
from render_firetruck_preview import Part, read_part, raster_view
from spagatti_shu_spec import (ATLAS_SIZE, LAMPS, REGIONS, SHAPE,
                               UV_PROJECTIONS, WHEEL_ANCHORS)

ROOT = Path(__file__).resolve().parents[1]
BLENDER = Path("/Applications/Blender.app/Contents/MacOS/Blender")
MODEL = ROOT / "assets/models/vehicles/spagatti_shu"
TEXTURE_DIR = ROOT / "assets/textures/vehicles/spagatti_shu"
TEXTURE = TEXTURE_DIR / "body.png"
IMAGEGEN_SOURCE = TEXTURE_DIR / "body-imagegen-source.png"
IMAGEGEN_TEMPLATE = ROOT / "build/spagatti-shu-imagegen-template.png"
UV_GUIDE = ROOT / "build/spagatti-shu-uv-guide.png"
REPORT = ROOT / "build/spagatti-shu-fit-report.json"
PREVIEW = ROOT / "build/spagatti-shu-preview.png"
BLOCKOUT_PREVIEW = ROOT / "build/spagatti-shu-blockout.png"

BASE_COLOURS = {
    "BODY_SIDE": (145, 24, 30, 255), "BODY_TOP": (184, 37, 39, 255),
    "BODY_FRONT": (160, 29, 34, 255), "BODY_REAR": (113, 17, 24, 255),
    "GLASS_SIDE": (25, 38, 48, 255), "GLASS_FRONT": (32, 49, 60, 255),
    "GLASS_REAR": (20, 31, 40, 255), "CLADDING": (17, 20, 23, 255),
    "BODY_SHADOW": (74, 13, 19, 255), "SEAM": (103, 82, 59, 255),
    "METAL": (151, 151, 139, 255), "BLACK": (9, 11, 13, 255),
    "HEADLIGHT": (224, 218, 185, 255), "TAIL_RED": (164, 34, 31, 255),
    "EXHAUST": (43, 44, 42, 255), "LENS_DARK": (20, 23, 27, 255),
}


def region_point(name: str, a: float, b: float) -> tuple[int, int]:
    """Convert a source-model coordinate pair to an atlas pixel."""
    _, ((alo, ahi), (blo, bhi)) = UV_PROJECTIONS[name]
    x0, y0, x1, y1 = REGIONS[name]
    x = x0 + 2 + (a - alo) / (ahi - alo) * (x1 - x0 - 4)
    y = y1 - 2 - (b - blo) / (bhi - blo) * (y1 - y0 - 4)
    return round(x), round(y)


def paint_end_details(image: Image.Image) -> None:
    """Paint flush grille/lamp/exhaust receivers in exact source coordinates."""
    draw = ImageDraw.Draw(image)
    rgba = image.mode == "RGBA"

    def colour(rgb):
        return (*rgb, 255) if rgba else rgb

    def polygon(name, coordinates, fill):
        draw.polygon([region_point(name, x, z) for x, z in coordinates],
                     fill=colour(fill))

    def ellipse(name, centre_x, centre_z, radius, fill, outline=None, width=1):
        a = region_point(name, centre_x - radius, centre_z - radius)
        b = region_point(name, centre_x + radius, centre_z + radius)
        box = (min(a[0], b[0]), min(a[1], b[1]),
               max(a[0], b[0]), max(a[1], b[1]))
        draw.ellipse(box, fill=colour(fill),
                     outline=colour(outline) if outline else None, width=width)

    # The horseshoe is texture on the curved front receiver. Its body-space
    # coordinates match the former cards, without their depth offset.
    polygon("BODY_FRONT", [(-.34, .22), (.34, .22), (.43, .34),
            (.39, .58), (.29, .68), (-.29, .68), (-.39, .58), (-.43, .34)],
            (77, 79, 76))
    polygon("BODY_FRONT", [(-.27, .25), (.27, .25), (.35, .35),
            (.32, .54), (.24, .61), (-.24, .61), (-.32, .54), (-.35, .35)],
            (7, 10, 13))
    for side in (-1., 1.):
        x0, x1 = sorted((side * .49, side * .92))
        polygon("BODY_FRONT", [(x0, .47), (x1, .49),
                (x1 * .96, .59), (x0 * .96, .63)], (22, 28, 32))
        ix0, ix1 = sorted((side * .53, side * .88))
        polygon("BODY_FRONT", [(ix0, .495), (ix1, .51),
                (ix1 * .96, .575), (ix0 * .96, .60)], (158, 157, 139))
        draw.line([region_point("BODY_FRONT", ix0, .575),
                   region_point("BODY_FRONT", ix1, .54)],
                  fill=colour((196, 193, 166)), width=1)

    polygon("BODY_REAR", [(-.96, .42), (.96, .42),
            (.89, .69), (-.89, .69)], (8, 11, 14))
    for side in (-1., 1.):
        for centre_x in (side * .60, side * .86):
            ellipse("BODY_REAR", centre_x, .56, .105, (83, 15, 18),
                    outline=(27, 9, 11))
            ellipse("BODY_REAR", centre_x, .56, .070, (139, 25, 25))
            ellipse("BODY_REAR", centre_x - side * .016, .578, .027,
                    (178, 42, 34))
    for centre_x in (-.17, .17):
        ellipse("BODY_REAR", centre_x, .25, .105, (19, 21, 21),
                outline=(142, 139, 127))
        ellipse("BODY_REAR", centre_x, .25, .064, (4, 6, 7))


def fill_texture(blockout: bool = False) -> Image.Image:
    """Make the exact receiver layout before imagegen adds surface character."""
    image = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (11, 14, 17, 255))
    draw = ImageDraw.Draw(image)
    if blockout:
        colours = {key: ((145, 143, 134, 255) if key.startswith("BODY_") else value)
                   for key, value in BASE_COLOURS.items()}
        colours["BODY_TOP"] = (162, 160, 149, 255)
        colours["BODY_SHADOW"] = (91, 91, 86, 255)
    else:
        colours = BASE_COLOURS
    for name, box in REGIONS.items():
        draw.rectangle(box, fill=colours[name])
    if blockout:
        return image

    # Main paint marks are authored in metres against the final source model.
    side = "BODY_SIDE"
    a, b = region_point(side, -2.40, .70), region_point(side, 2.40, .80)
    draw.rectangle((a[0], min(a[1], b[1]), b[0], max(a[1], b[1])),
                   fill=(183, 42, 42, 255))
    a, b = region_point(side, -2.40, .18), region_point(side, 2.40, .36)
    draw.rectangle((a[0], min(a[1], b[1]), b[0], max(a[1], b[1])),
                   fill=(91, 15, 23, 255))
    draw.line([region_point(side, -2.28, .56), region_point(side, -1.38, .70),
               region_point(side, -.15, .55), region_point(side, 1.15, .52),
               region_point(side, 2.25, .47)], fill=(217, 67, 55, 255), width=1)

    top = "BODY_TOP"
    draw.line([region_point(top, -.26, -2.30), region_point(top, -.18, 2.25)],
              fill=(226, 74, 60, 255), width=2)
    draw.line([region_point(top, .26, -2.30), region_point(top, .18, 2.25)],
              fill=(226, 74, 60, 255), width=2)
    for name in ("BODY_FRONT", "BODY_REAR"):
        a, b = region_point(name, -1.07, .18), region_point(name, 1.07, .38)
        draw.rectangle((a[0], b[1], b[0], a[1]), fill=(80, 13, 20, 255))
    for name in ("GLASS_SIDE", "GLASS_FRONT", "GLASS_REAR"):
        x0, y0, x1, y1 = REGIONS[name]
        draw.line((x0 + 5, y1 - 5, x1 - 5, y0 + 7),
                  fill=(59, 81, 91, 255), width=2)
        draw.line((x0 + 5, y0 + 5, x1 - 5, y0 + 5),
                  fill=(73, 96, 104, 255), width=1)
    paint_end_details(image)
    return image


def recolour_body_red(image: Image.Image) -> None:
    """Keep imagegen's value structure while locking body receivers to red."""
    pixels = np.asarray(image.convert("RGBA")).copy()
    ramp = np.asarray([
        (65, 9, 16, 255), (83, 12, 19, 255), (105, 16, 23, 255),
        (130, 21, 28, 255), (155, 28, 33, 255), (180, 37, 39, 255),
        (204, 49, 45, 255), (226, 67, 55, 255), (239, 89, 70, 255),
    ], dtype=np.uint8)
    offsets = {"BODY_SIDE": 0, "BODY_TOP": 1, "BODY_FRONT": 0,
               "BODY_REAR": -1, "BODY_SHADOW": -2}
    for name, offset in offsets.items():
        x0, y0, x1, y1 = REGIONS[name]
        crop = pixels[y0:y1 + 1, x0:x1 + 1]
        luminance = (crop[:, :, 0].astype(float) * .2126 +
                     crop[:, :, 1].astype(float) * .7152 +
                     crop[:, :, 2].astype(float) * .0722)
        low, high = np.percentile(luminance, (4, 96))
        if high - low < 1:
            levels = np.full(luminance.shape, 4, dtype=int)
        else:
            levels = np.rint((luminance - low) / (high - low) * 6).astype(int) + 1
        levels = np.clip(levels + offset, 0, len(ramp) - 1)
        crop[:] = ramp[levels]
    image.paste(Image.fromarray(pixels))


def make_imagegen_template(vertices, indices) -> None:
    """Write a 4x exact-UV edit target after the final mesh exists."""
    scale = 4
    image = fill_texture().resize((ATLAS_SIZE * scale, ATLAS_SIZE * scale),
                                  Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(image)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial Bold.ttf", 15)
    except OSError:
        font = ImageFont.load_default()
    for name, box in REGIONS.items():
        scaled = tuple(value * scale for value in box)
        draw.rectangle(scaled, outline=(255, 55, 187, 255), width=3)
        draw.text((scaled[0] + 6, scaled[1] + 5), name,
                  fill=(255, 245, 250, 255), stroke_width=2,
                  stroke_fill=(15, 10, 18, 255), font=font)
    for triangle in indices:
        points = [(float(vertices[index][6]) * ATLAS_SIZE * scale,
                   (1 - float(vertices[index][7])) * ATLAS_SIZE * scale)
                  for index in triangle]
        draw.line((*points, points[0]), fill=(54, 255, 210, 225), width=2)
    IMAGEGEN_TEMPLATE.parent.mkdir(parents=True, exist_ok=True)
    image.save(IMAGEGEN_TEMPLATE)


def make_texture() -> None:
    """Reduce the imagegen edit of the exact UV template to a crisp atlas."""
    if not IMAGEGEN_SOURCE.is_file():
        raise FileNotFoundError(f"imagegen atlas source missing: {IMAGEGEN_SOURCE}")
    with Image.open(IMAGEGEN_SOURCE) as source:
        source = source.convert("RGB")
        if source.width != source.height:
            raise ValueError(f"imagegen source must stay square: {source.size}")
        reduced = source.resize((ATLAS_SIZE, ATLAS_SIZE), Image.Resampling.BOX)
    composed = Image.new("RGB", (ATLAS_SIZE, ATLAS_SIZE), (11, 14, 17))
    for box in REGIONS.values():
        crop_box = (box[0], box[1], box[2] + 1, box[3] + 1)
        composed.paste(reduced.crop(crop_box), crop_box)
    image = composed.quantize(colors=60, method=Image.Quantize.MEDIANCUT,
                              dither=Image.Dither.NONE).convert("RGBA")
    # Imagegen supplies the broad value character. The model-locked red grade
    # makes the body/glass boundary unambiguous without disturbing glass cells.
    recolour_body_red(image)
    # Critical lamps and grille are locked after palette reduction so their
    # emissive source colours stay exact instead of merging into body reds.
    paint_end_details(image)
    TEXTURE_DIR.mkdir(parents=True, exist_ok=True)
    image.save(TEXTURE, optimize=True)


def make_uv_guide(vertices, indices) -> None:
    image = Image.open(TEXTURE).convert("RGBA").resize((768, 768),
                                                       Image.Resampling.NEAREST)
    draw = ImageDraw.Draw(image)
    for triangle in indices:
        points = [(float(vertices[index][6]) * 768,
                   (1 - float(vertices[index][7])) * 768)
                  for index in triangle]
        draw.line((*points, points[0]), fill=(80, 255, 205, 220), width=1)
    UV_GUIDE.parent.mkdir(parents=True, exist_ok=True)
    image.save(UV_GUIDE)


def projected_triangle_contains(point, triangle) -> bool:
    def signed(a, b, c):
        return ((a[0] - c[0]) * (b[1] - c[1]) -
                (b[0] - c[0]) * (a[1] - c[1]))
    values = [signed(point, triangle[i], triangle[(i + 1) % 3])
              for i in range(3)]
    return all(value > 1e-6 for value in values) or all(value < -1e-6 for value in values)


def validate(mesh: Path, texture_source: str):
    vertices, indices = read_mesh(mesh)
    positions = [vertex[:3] for vertex in vertices]
    triangles = len(indices)
    mins = [min(point[axis] for point in positions) for axis in range(3)]
    maxs = [max(point[axis] for point in positions) for axis in range(3)]
    sizes = [maxs[axis] - mins[axis] for axis in range(3)]
    if not 650 <= triangles <= 1300:
        raise ValueError(f"triangle budget missed: {triangles}")
    if any(not math.isfinite(value) for vertex in vertices for value in vertex):
        raise ValueError("non-finite mesh")
    if any(not 0 <= value <= 1 for vertex in vertices for value in vertex[6:8]):
        raise ValueError("UV outside atlas")
    if abs(mins[0] + maxs[0]) > .04 or abs(mins[2] + maxs[2]) > .04:
        raise ValueError("body not centered")
    if not 2.10 <= sizes[0] <= 2.22 or not 1.04 <= sizes[1] <= 1.12 or not 4.75 <= sizes[2] <= 4.90:
        raise ValueError(f"bad body bounds {sizes}")
    if mins[1] < .10:
        raise ValueError("wheel or ground-cutting geometry in body")

    # No grille or lamp cards may sit ahead of the curved receiver skin.
    if any(vertex[2] > 2.401 and .44 <= vertex[1] <= .66 and
           .45 <= abs(vertex[0]) <= .95 for vertex in positions):
        raise ValueError("front lamp card protrudes beyond body receiver")
    if any(vertex[2] < -2.401 and .43 <= vertex[1] <= .68 and
           .47 <= abs(vertex[0]) <= .98 for vertex in positions):
        raise ValueError("rear lamp card protrudes beyond body receiver")

    # Glass is separate closed geometry fitted inside solid frames. Classify
    # triangles by their semantic UV receiver so a solid painted greenhouse or
    # a return to fixed-depth side cards fails the cook.
    def triangles_in_region(name):
        x0, y0, x1, y1 = REGIONS[name]
        result = []
        for triangle in indices:
            texels = [(vertices[index][6] * ATLAS_SIZE,
                       (1 - vertices[index][7]) * ATLAS_SIZE)
                      for index in triangle]
            if all(x0 + 1 <= u <= x1 - 1 and y0 + 1 <= v <= y1 - 1
                   for u, v in texels):
                result.append(triangle)
        return result

    glass = {name: triangles_in_region(name) for name in
             ("GLASS_SIDE", "GLASS_FRONT", "GLASS_REAR")}
    if len(glass["GLASS_SIDE"]) < 40 or len(glass["GLASS_FRONT"]) < 10 or \
            len(glass["GLASS_REAR"]) < 10:
        raise ValueError("missing closed two-sided window geometry")
    side_points = [positions[index] for triangle in glass["GLASS_SIDE"]
                   for index in triangle]
    if min(abs(point[0]) for point in side_points) > .54 or \
            max(abs(point[0]) for point in side_points) > .715:
        raise ValueError("side windows do not follow the tapered greenhouse")
    front_points = [positions[index] for triangle in glass["GLASS_FRONT"]
                    for index in triangle]
    rear_points = [positions[index] for triangle in glass["GLASS_REAR"]
                   for index in triangle]
    if not .19 <= min(point[2] for point in front_points) <= .25 or \
            not .78 <= max(point[2] for point in front_points) <= .82:
        raise ValueError("windshield is not fitted between cowl and roof")
    if not -1.05 <= min(point[2] for point in rear_points) <= -1.02 or \
            not -.48 <= max(point[2] for point in rear_points) <= -.43:
        raise ValueError("rear window is not fitted between roof and deck")

    def geometric_normal(triangle):
        a, b, c = (np.asarray(positions[index], dtype=float)
                   for index in triangle)
        return np.cross(b - a, c - a)

    for side in (-1., 1.):
        facing = [geometric_normal(triangle)[0]
                  for triangle in glass["GLASS_SIDE"]
                  if all(side * positions[index][0] > .45 for index in triangle)]
        if not facing or min(facing) >= -1e-5 or max(facing) <= 1e-5:
            raise ValueError(f"side glass is not two-sided on side {side}")
    for name in ("GLASS_FRONT", "GLASS_REAR"):
        facing = [geometric_normal(triangle)[2] for triangle in glass[name]]
        if min(facing) >= -1e-5 or max(facing) <= 1e-5:
            raise ValueError(f"{name} is not two-sided")

    glass_keys = {tuple(int(i) for i in triangle)
                  for triangles in glass.values() for triangle in triangles}

    def covered(point, axes, predicate, allowed):
        matches = []
        for triangle in indices:
            key = tuple(int(index) for index in triangle)
            if key not in allowed and not predicate(triangle):
                continue
            projected = [(positions[index][axes[0]], positions[index][axes[1]])
                         for index in triangle]
            if projected_triangle_contains(point, projected):
                matches.append(key)
        return matches

    side_samples = ((.0, 1.00), (-.76, .96))
    for side in (-1., 1.):
        allowed = {tuple(int(i) for i in triangle)
                   for triangle in glass["GLASS_SIDE"]
                   if all(side * positions[i][0] > .45 for i in triangle)}
        for sample in side_samples:
            if not covered(sample, (2, 1), lambda _: False, allowed):
                raise ValueError(f"side glass misses framed opening {side}, {sample}")
            solids = covered(sample, (2, 1),
                             lambda triangle: tuple(int(i) for i in triangle) not in glass_keys and
                             all(side * positions[i][0] > .45 and positions[i][1] > .80
                                 for i in triangle), set())
            if solids:
                raise ValueError(f"painted cabin remains behind side glass {side}, {sample}")

    for name, sample, y_range in (
        ("GLASS_FRONT", (.10, 1.00), (.12, .90)),
        ("GLASS_REAR", (.10, 1.00), (-1.12, -.38)),
    ):
        allowed = {tuple(int(i) for i in triangle) for triangle in glass[name]}
        if not covered(sample, (0, 1), lambda _: False, allowed):
            raise ValueError(f"{name} misses its framed opening")
        solids = covered(sample, (0, 1),
                         lambda triangle: tuple(int(i) for i in triangle) not in glass_keys and
                         all(y_range[0] <= positions[i][2] <= y_range[1] and
                             positions[i][1] > .82 for i in triangle), set())
        if solids:
            raise ValueError(f"painted cabin remains behind {name}")

    for wheel_z in (WHEEL_ANCHORS["front_z"], WHEEL_ANCHORS["rear_z"]):
        for side in (-1., 1.):
            for z_offset, y_offset in ((0, 0), (-.05, 0), (.05, 0),
                                       (0, -.05), (0, .05)):
                sample = (wheel_z + z_offset, WHEEL_ANCHORS["arch_y"] + y_offset)
                for triangle_indices in indices:
                    triangle = [positions[index] for index in triangle_indices]
                    if not all(side * vertex[0] > .70 for vertex in triangle):
                        continue
                    if projected_triangle_contains(sample, [(v[2], v[1]) for v in triangle]):
                        raise ValueError(f"solid side covers wheel opening {sample}")
    for hood_x in (-.52, 0, .52):
        for hood_z in (WHEEL_ANCHORS["front_z"] - .05,
                       WHEEL_ANCHORS["front_z"] + .05):
            if not any(all(positions[index][1] > .62 for index in triangle) and
                       projected_triangle_contains(
                           (hood_x, hood_z),
                           [(positions[index][0], positions[index][2]) for index in triangle])
                       for triangle in indices):
                raise ValueError(f"central hood missing at {hood_x}, {hood_z}")
    with Image.open(TEXTURE) as image:
        if image.size != (256, 256) or image.mode != "RGBA":
            raise ValueError("atlas must be 256x256 RGBA")
        if image.getchannel("A").getextrema() != (255, 255):
            raise ValueError("atlas must be opaque")
        palette = len(set(image.getdata()))
        if palette > 96:
            raise ValueError(f"too many PSX colours: {palette}")
        paint = image.getpixel(region_point("BODY_SIDE", 0, .65))
        if paint[0] < max(90, paint[1] * 2, paint[2] * 1.6):
            raise ValueError(f"body diagnostic paint is not red: {paint}")
        for x0, x1 in LAMPS["headlights"]["x"]:
            head = image.getpixel(region_point("BODY_FRONT", (x0 + x1) * .5, .55))
            if min(head[:3]) < 120:
                raise ValueError(f"headlight receiver is not pale: {head}")
        for x0, x1 in LAMPS["brakelights"]["x"]:
            tail = image.getpixel(region_point("BODY_REAR", (x0 + x1) * .5, .56))
            if tail[0] < max(80, tail[1] * 2, tail[2] * 1.7):
                raise ValueError(f"brake receiver is not red: {tail}")
        grille = image.getpixel(region_point("BODY_FRONT", 0, .43))
        if max(grille[:3]) > 45:
            raise ValueError(f"grille receiver is not dark: {grille}")
    make_uv_guide(vertices, indices)
    report = {
        "asset": SHAPE["name"], "vertices": len(vertices), "triangles": triangles,
        "bounds_min": [round(float(x), 5) for x in mins],
        "bounds_max": [round(float(x), 5) for x in maxs],
        "bounds_size": [round(float(x), 5) for x in sizes],
        "atlas": SHAPE["atlas"], "palette_colours": palette,
        "texture_source": texture_source,
        "uv_contract": "final model -> source-coordinate projections -> imagegen edit",
        "wheel_anchors": WHEEL_ANCHORS, "lamp_regions": LAMPS,
        "art_direction": SHAPE["traits"],
        "checks": ["joined wheel-less body", "four real open wheel wells",
                   "preserved centre hood", "independent Spagatti loft",
                   "framed two-sided windshield, side and rear glass",
                   "model-sized side/top/front/rear/glass UV receivers",
                   "flush painted grille, headlamp and brake-lamp receivers",
                   "opaque limited-palette PSX atlas"],
    }
    REPORT.parent.mkdir(exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return vertices, indices


def preview_parts(steer: float = 0) -> list[Part]:
    body = read_part(MODEL / "body.emesh", TEXTURE)
    wheel = read_part(ROOT / "assets/models/vehicles/common/wheel.emesh",
                      ROOT / "assets/textures/vehicles/common/wheel.png")
    radius = max(np.ptp(wheel.positions[:, 1]), np.ptp(wheel.positions[:, 2])) * .5
    result = [body]
    for axle in (WHEEL_ANCHORS["front_z"], WHEEL_ANCHORS["rear_z"]):
        for side in (-1., 1.):
            angle = steer if axle > 0 else 0
            cosine, sine = math.cos(angle), math.sin(angle)
            rotation = np.array([[cosine, 0, -sine], [0, 1, 0], [sine, 0, cosine]])
            positions = wheel.positions * (WHEEL_ANCHORS["radius"] / radius)
            positions = positions @ rotation.T
            positions += np.array([side * WHEEL_ANCHORS["x"],
                                   WHEEL_ANCHORS["arch_y"], axle])
            result.append(Part(positions, wheel.normals @ rotation.T, wheel.uvs,
                               wheel.indices, wheel.texture))
    return result


def previews(blockout: bool) -> None:
    straight, locked = preview_parts(), preview_parts(.72)
    views = [("FRONT 3/4", straight, -32, 18), ("SIDE", straight, -90, 0),
             ("REAR 3/4", straight, -148, 18),
             ("ELEVATED FRONT", straight, -32, 38),
             ("FULL LOCK", locked, -32, 25), ("REAR", straight, 180, 0)]
    sheet = Image.new("RGB", (1440, 840), (19, 21, 25))
    draw = ImageDraw.Draw(sheet)
    for index, (label, items, yaw, pitch) in enumerate(views):
        x, y = (index % 3) * 480, (index // 3) * 420
        # NumPy 2.4 can emit bogus floating-point matmul warnings for the tiny
        # shared-wheel arrays even though the validated inputs and output are
        # finite. Keep the reproducible QA log readable.
        with np.errstate(all="ignore"):
            shot = raster_view(items, yaw, pitch, 480, 386)
        sheet.paste(shot, (x, y))
        draw.text((x + 12, y + 397), label, fill=(230, 224, 205))
    sheet.save(BLOCKOUT_PREVIEW if blockout else PREVIEW)


def build_model() -> None:
    MODEL.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(BLENDER), "--background", "--factory-startup", "--python",
                    str(ROOT / "tools/spagatti_shu_blender.py"), "--",
                    "--mesh", str(MODEL / "body.emesh"),
                    "--blend", str(MODEL / "source.blend")], cwd=ROOT, check=True)


def run_asset_lab() -> None:
    screenshot = ROOT / "build/spagatti-shu-engine.png"
    log = ROOT / "build/spagatti-shu-engine.log"
    proc = subprocess.run([str(ROOT / "build/bin/apricot_asset_lab"),
                           "--model", str(MODEL / "body.emesh"),
                           "--texture", str(TEXTURE), "--yaw", "212",
                           "--frames", "60", "--screenshot", str(screenshot)],
                          cwd=ROOT, capture_output=True, text=True)
    log.write_text(proc.stdout + proc.stderr)
    proc.check_returncode()
    if "0 GL errors" not in proc.stdout:
        raise RuntimeError(proc.stdout)
    print("engine-body 60 frames, 0 GL errors")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--blockout", action="store_true")
    parser.add_argument("--prepare-imagegen", action="store_true")
    parser.add_argument("--texture-only", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--preview-only", action="store_true")
    parser.add_argument("--asset-lab", action="store_true")
    args = parser.parse_args()
    MODEL.mkdir(parents=True, exist_ok=True)
    TEXTURE_DIR.mkdir(parents=True, exist_ok=True)
    if not (args.texture_only or args.validate_only or args.preview_only):
        build_model()
    mesh = MODEL / "body.emesh"
    vertices, indices = read_mesh(mesh)
    make_imagegen_template(vertices, indices)
    if args.prepare_imagegen or args.blockout:
        fill_texture(True).save(TEXTURE)
        source = "model-derived neutral blockout"
    elif not (args.validate_only or args.preview_only):
        make_texture()
        source = "built-in imagegen edit of final UV template with locked red grade"
    else:
        source = "existing cooked texture"
    validate(mesh, source)
    previews(args.prepare_imagegen or args.blockout)
    if args.asset_lab:
        run_asset_lab()


if __name__ == "__main__":
    main()
