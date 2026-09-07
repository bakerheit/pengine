#!/usr/bin/env python3
"""Model-first texture cook, validation, and previews for Cruiser 91E."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

import make_municipal_cruiser_91a_assets as base
from municipal_cruiser_91e_spec import (
    ATLAS_SIZE, DOOR, DRIVER, LAMPS, LIGHTBAR, PANE_FILES, REGIONS, SHAPE,
    WHEELS,
)

ROOT = Path(__file__).resolve().parents[1]
BLENDER = Path("/Applications/Blender.app/Contents/MacOS/Blender")
MODEL = ROOT / "assets/models/vehicles/municipal_cruiser_91e"
TEXTURE_DIR = ROOT / "assets/textures/vehicles/municipal_cruiser_91e"
TEXTURE = TEXTURE_DIR / "body.png"
IMAGEGEN_SOURCE = TEXTURE_DIR / "body-imagegen-source.png"
PROMPT = ROOT / "tools/municipal_cruiser_91e_texture_prompt.md"
TEMPLATE = ROOT / "build/municipal-cruiser-91e-imagegen-template.png"
UV_GUIDE = ROOT / "build/municipal-cruiser-91e-uv-guide.png"
REPORT = ROOT / "build/municipal-cruiser-91e-fit-report.json"
PREVIEW = ROOT / "build/municipal-cruiser-91e-preview.png"
OPEN_PREVIEW = ROOT / "build/municipal-cruiser-91e-open-door-preview.png"
BLOCKOUT_PREVIEW = ROOT / "build/municipal-cruiser-91e-blockout.png"

BASE_COLOURS = {
    "BODY_SIDE": (48, 25, 19, 255),
    "BODY_TOP": (219, 201, 154, 255),
    "DOOR_WHITE": (213, 194, 148, 255),
    "POLICE": (225, 207, 160, 255),
    "GLASS": (32, 55, 67, 150),
    "FRONT": (38, 25, 21, 255),
    "REAR": (31, 20, 18, 255),
    "METAL": (147, 145, 132, 255),
    "RUBBER": (18, 15, 14, 255),
    "INTERIOR": (48, 39, 33, 255),
    "SEAM": (14, 10, 9, 255),
    "HEADLIGHT": (226, 218, 181, 255),
    "AMBER": (205, 112, 27, 255),
    "TAIL_RED": (156, 26, 29, 255),
    "REVERSE": (204, 209, 188, 255),
    "LIGHTBAR_RED": (174, 18, 30, 255),
    "LIGHTBAR_BLUE": (24, 55, 162, 255),
    "SHADOW": (11, 9, 9, 255),
    "BLACK": (7, 7, 8, 255),
}

RAMPS = {
    "BODY_SIDE": [(17, 10, 9), (28, 15, 12), (42, 22, 17),
                  (58, 30, 22), (75, 40, 28), (94, 53, 36)],
    "BODY_TOP": [(105, 88, 61), (145, 124, 87), (181, 158, 113),
                 (210, 190, 146), (231, 215, 174)],
    "DOOR_WHITE": [(100, 83, 58), (142, 121, 84), (178, 155, 109),
                   (207, 188, 144), (232, 216, 175)],
    "POLICE": [(111, 93, 64), (151, 129, 90), (187, 163, 116),
               (216, 197, 150), (237, 220, 179)],
    "GLASS": [(9, 20, 26), (17, 35, 43), (28, 54, 65),
              (44, 73, 84), (66, 98, 106)],
    "FRONT": [(15, 10, 9), (29, 19, 16), (45, 30, 24), (64, 43, 32)],
    "REAR": [(12, 8, 8), (25, 15, 14), (40, 25, 21), (57, 37, 29)],
    "METAL": [(37, 39, 38), (72, 76, 75), (111, 115, 111),
              (154, 156, 147), (203, 202, 184)],
    "RUBBER": [(6, 6, 7), (13, 12, 12), (23, 21, 20), (39, 35, 32)],
    "INTERIOR": [(16, 13, 12), (29, 24, 21), (45, 37, 31), (65, 53, 42)],
    "SEAM": [(4, 4, 5), (10, 8, 8), (19, 14, 12)],
    "SHADOW": [(4, 4, 5), (9, 8, 8), (17, 13, 12)],
    "BLACK": [(3, 4, 5), (7, 7, 8), (13, 12, 12)],
}

# Bind the reusable exact-template, quantization, and preview helpers to 91E.
for name, value in {
    "ROOT": ROOT, "MODEL": MODEL, "TEXTURE_DIR": TEXTURE_DIR,
    "TEXTURE": TEXTURE, "IMAGEGEN_SOURCE": IMAGEGEN_SOURCE,
    "PROMPT": PROMPT, "TEMPLATE": TEMPLATE, "UV_GUIDE": UV_GUIDE,
    "REPORT": REPORT, "PREVIEW": PREVIEW, "OPEN_PREVIEW": OPEN_PREVIEW,
    "BLOCKOUT_PREVIEW": BLOCKOUT_PREVIEW, "ATLAS_SIZE": ATLAS_SIZE,
    "DOOR": DOOR, "DRIVER": DRIVER, "LAMPS": LAMPS, "LIGHTBAR": LIGHTBAR,
    "PANE_FILES": PANE_FILES, "REGIONS": REGIONS, "SHAPE": SHAPE,
    "WHEELS": WHEELS, "BASE_COLOURS": BASE_COLOURS, "RAMPS": RAMPS,
}.items():
    setattr(base, name, value)


def paint_locked_details(image: Image.Image) -> None:
    """Lock the patrol livery, text, and receiver colors after reduction."""
    draw = ImageDraw.Draw(image)
    # Imagegen is allowed to shade the mapped components, but only the locked
    # POLICE receiver may contain text. Clear any eager pseudo-lettering from
    # the generic cream/roof cell while retaining a period pixel paint finish.
    a, b, c, d = REGIONS["DOOR_WHITE"]
    draw.rectangle((a, b, c, d), fill=(202, 182, 137, 255))
    draw.rectangle((a + 2, b + 2, c - 2, b + 5), fill=(228, 211, 169, 255))
    draw.line((a + 3, b + 15, c - 3, b + 15), fill=(185, 163, 118, 255))
    draw.line((a + 3, d - 9, c - 3, d - 9), fill=(157, 134, 94, 255))
    for x in range(a + 7, c - 3, 13):
        draw.point((x, b + 26), fill=(218, 199, 155, 255))

    x0, y0, x1, y1 = REGIONS["POLICE"]
    draw.rectangle((x0 + 2, y0 + 2, x1 - 2, y1 - 2),
                   fill=(222, 204, 157, 255))
    draw.rectangle((x0 + 3, y0 + 8, x1 - 3, y0 + 12),
                   fill=(64, 31, 20, 255))
    draw.rectangle((x0 + 3, y1 - 11, x1 - 3, y1 - 7),
                   fill=(64, 31, 20, 255))
    mask = base.binary_text_mask("POLICE", (x1 - x0 - 14, 25))
    image.paste((56, 27, 18, 255), (x0 + 7, y0 + 22), mask)

    def lens(name: str, dark, colour, bright) -> None:
        a, b, c, d = REGIONS[name]
        draw.rectangle((a, b, c, d), fill=(*dark, 255))
        draw.rectangle((a + 3, b + 3, c - 3, d - 3), fill=(*colour, 255))
        draw.rectangle((a + 5, b + 5, c - 5, b + 7), fill=(*bright, 255))
        draw.rectangle((a + 5, d - 7, c - 5, d - 5), fill=(*dark, 255))
        for x in range(a + 7, c - 4, 4):
            draw.line((x, b + 5, x, d - 5), fill=(*bright, 255), width=1)

    lens("HEADLIGHT", (49, 50, 45), (190, 187, 157), (244, 235, 195))
    lens("AMBER", (73, 36, 9), (197, 102, 17), (249, 166, 43))
    lens("TAIL_RED", (57, 8, 13), (154, 20, 28), (238, 58, 53))
    lens("REVERSE", (48, 52, 49), (181, 188, 171), (239, 237, 210))
    lens("LIGHTBAR_RED", (58, 4, 13), (166, 13, 29), (255, 79, 70))
    lens("LIGHTBAR_BLUE", (6, 17, 62), (19, 52, 160), (76, 143, 255))


base.paint_locked_details = paint_locked_details


def build_model() -> None:
    MODEL.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        str(BLENDER), "--background", "--factory-startup", "--python",
        str(ROOT / "tools/municipal_cruiser_91e_blender.py"), "--",
        "--mesh", str(MODEL / "body.emesh"),
        "--blend", str(MODEL / "source.blend"),
    ], cwd=ROOT, check=True)


def validate(texture_source: str, final_texture: bool = True):
    meshes = {name: base.check_mesh(MODEL / (name + ".emesh"))
              for name in ("body", "body_open", "driver_door", *PANE_FILES)}
    vertices, indices, triangles = meshes["body"]
    if not SHAPE["triangle_budget"][0] <= len(indices) <= SHAPE["triangle_budget"][1]:
        raise ValueError(f"triangle budget missed: {len(indices)}")
    minimum, maximum = vertices[:, :3].min(0), vertices[:, :3].max(0)
    size = maximum - minimum
    if not np.allclose((minimum[0], minimum[2]), (-1.086, -2.78), atol=.004):
        raise ValueError(f"unexpected lower plan bounds: {minimum}")
    if not np.allclose((maximum[0], maximum[2]), (1.086, 2.78), atol=.004):
        raise ValueError(f"unexpected upper plan bounds: {maximum}")
    if minimum[1] < .10 or not 1.69 <= maximum[1] <= 1.71:
        raise ValueError(f"unexpected vertical bounds: {minimum}, {maximum}")

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
                    if any(base.projected(point, tri[:, [2, 1]]) for tri in relevant):
                        raise ValueError(f"blocked wheel opening: {side}, {point}")
                    opening_samples += 1

    hood_deck_samples = 0
    high = triangles[(triangles[:, :, 1] > .80).all(1)]
    for axle in (WHEELS["front_z"], WHEELS["rear_z"]):
        for x in (-.56, 0, .56):
            for z in (axle - .05, axle + .05):
                if not any(base.projected((x, z), tri[:, [0, 2]]) for tri in high):
                    raise ValueError(f"centre hood/deck missing: {(x, z)}")
                hood_deck_samples += 1

    open_side = meshes["body_open"][2][
        (meshes["body_open"][2][:, :, 0] > .82).all(1)]
    door_side = meshes["driver_door"][2][
        (meshes["driver_door"][2][:, :, 0] > .96).all(1)]
    doorway_samples = 0
    for forward in (-.14, .10, .38, .66):
        for height in (.56, .74, .93):
            point = (forward, height)
            if any(base.projected(point, tri[:, [2, 1]]) for tri in open_side):
                raise ValueError(f"driver doorway still solid: {point}")
            if not any(base.projected(point, tri[:, [2, 1]]) for tri in door_side):
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

    angle = math.radians(DOOR["open_degrees"])
    c, s = math.cos(angle), math.sin(angle)
    rotation = np.array(((c, 0, s), (0, 1, 0), (-s, 0, c)))
    hinge, handle = np.asarray(DOOR["hinge"]), np.asarray(DOOR["handle"])
    opened_handle = (handle - hinge) @ rotation.T + hinge
    if opened_handle[0] < handle[0] + .64:
        raise ValueError("driver door rotates inward")
    glass_points = meshes["driver_glass"][0][:, :3]
    opened_glass = (glass_points - hinge) @ rotation.T + hinge
    if opened_glass[:, 0].mean() < glass_points[:, 0].mean() + .27:
        raise ValueError("driver_glass does not follow outward door transform")

    with Image.open(TEXTURE) as atlas:
        if atlas.size != (256, 256) or atlas.mode != "RGBA":
            raise ValueError("atlas must be 256x256 RGBA")
        colours = len(set(atlas.getdata()))
        if colours > 96:
            raise ValueError(f"palette too broad: {colours}")
        gx0, gy0, gx1, gy1 = REGIONS["GLASS"]
        extrema = atlas.getchannel("A").crop((gx0, gy0, gx1 + 1, gy1 + 1)).getextrema()
        if final_texture and extrema != (158, 158):
            raise ValueError(f"glass cell is not semi-transparent: {extrema}")

    base.make_uv_guide(vertices, indices)
    report = {
        "asset": SHAPE["name"],
        "mesh": str(MODEL / "body.emesh"),
        "texture": str(TEXTURE),
        "vertices": len(vertices), "triangles": len(indices),
        "bounds_min": [round(float(v), 5) for v in minimum],
        "bounds_max": [round(float(v), 5) for v in maximum],
        "bounds_size": [round(float(v), 5) for v in size],
        "shape_contract": SHAPE, "wheel_anchors": WHEELS,
        "door": DOOR, "driver": DRIVER,
        "panes": {name: {"file": str(MODEL / (name + ".emesh")),
                          "triangles": len(meshes[name][1])} for name in PANE_FILES},
        "articulated_triangles": {
            "body_open": len(meshes["body_open"][1]),
            "driver_door": len(meshes["driver_door"][1]),
            "panes": sum(len(meshes[name][1]) for name in PANE_FILES),
            "total": component_triangles,
        },
        "lamp_receivers": LAMPS, "lightbar_receivers": LIGHTBAR,
        "atlas": [256, 256, "RGBA"], "atlas_regions": REGIONS,
        "palette_colours": colours,
        "glass_alpha": 158 if final_texture else "pending final cook",
        "texture_source": texture_source,
        "imagegen": {
            "mode": "built-in imagegen edit", "template": str(TEMPLATE),
            "prompt_file": str(PROMPT), "output_source": str(IMAGEGEN_SOURCE),
            "source_sha256": (hashlib.sha256(IMAGEGEN_SOURCE.read_bytes()).hexdigest()
                              if IMAGEGEN_SOURCE.is_file() else None),
            "contract": "square 4x final-mesh UV template; fixed receivers",
        },
        "samples": {"wheel_openings": opening_samples,
                    "hood_deck": hood_deck_samples,
                    "driver_doorway": doorway_samples},
        "checks": [
            "joined wheel-less closed body", "four true side wheel openings",
            "intact long hood and deck", "hollow four-seat cabin",
            "separate front-hinged driver door and six capped panes",
            "driver glass follows the door hinge", "semi-transparent glass",
            "post-reduction POLICE and lamp overlays",
            "actual shared-wheel closed and open-door previews",
        ],
        "sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                   for path in [MODEL / "body.emesh", MODEL / "body_open.emesh",
                                MODEL / "driver_door.emesh",
                                *[MODEL / (n + ".emesh") for n in PANE_FILES], TEXTURE]},
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"triangles": len(indices),
                      "articulated_triangles": report["articulated_triangles"],
                      "bounds_size": report["bounds_size"],
                      "palette_colours": colours,
                      "samples": report["samples"]}, indent=2))
    return report


def run_asset_lab() -> None:
    executable = ROOT / "build/bin/apricot_asset_lab"
    screenshot = ROOT / "build/municipal-cruiser-91e-engine.png"
    log = ROOT / "build/municipal-cruiser-91e-engine.log"
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
        base.fill_neutral_atlas().save(TEXTURE)
    elif not (args.validate_only or args.preview_only):
        base.make_texture()
    if not (args.texture_only or args.validate_only or args.preview_only):
        build_model()
    vertices, indices = base.read_mesh(MODEL / "body.emesh")
    base.make_template(vertices, indices)
    if not args.preview_only:
        validate("model-derived neutral UV template" if args.prepare_imagegen
                 else "built-in imagegen edit of exact final UV template",
                 final_texture=not args.prepare_imagegen)
    base.previews(args.prepare_imagegen)
    if args.asset_lab and not args.prepare_imagegen:
        run_asset_lab()


if __name__ == "__main__":
    main()
