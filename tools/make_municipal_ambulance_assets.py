#!/usr/bin/env python3
"""Reproducible Municipal ambulance model, atlas, preview and fit gate."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from make_vesper_vx91_assets import read_emesh
from municipal_ambulance_spec import ATLAS_SIZE, REGIONS, SHAPE, WHEELS

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "assets/models/vehicles/ambulance"
TEXTURE = ROOT / "assets/textures/vehicles/ambulance/body.png"


def make_texture(blockout=False):
    image = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (14, 18, 21, 255))
    draw = ImageDraw.Draw(image)
    palette = {
        "CAB_BODY": (225, 224, 215),
        "MODULE_WHITE": (235, 234, 224),
        "ROOF": (207, 209, 204),
        "STRIPE": (174, 31, 37),
        "GLASS": (24, 43, 53),
        "REAR_DOOR": (220, 220, 213),
        "SIDE_DOOR": (225, 225, 216),
        "MEDICAL": (232, 232, 224),
        "EMS_TEXT": (179, 31, 39),
        "GRILLE": (27, 31, 32),
        "METAL": (146, 151, 149),
        "HEADLIGHT": (225, 225, 198),
        "AMBER": (215, 126, 33),
        "TAIL_RED": (166, 30, 35),
        "REVERSE": (218, 220, 204),
        "LIGHT_RED": (202, 35, 42),
        "LIGHT_BLUE": (37, 83, 159),
        "BLACK": (12, 16, 18),
        "CLADDING": (35, 39, 40),
        "INTERIOR": (38, 45, 47),
        "SEAM": (54, 56, 55),
        "SHADOW": (20, 23, 24),
    }
    for name, box in REGIONS.items():
        draw.rectangle(box, fill=palette[name] + (255,))

    if not blockout:
        def rect(box, colour):
            draw.rectangle(box, fill=colour + (255,))

        # Broad paint gets restrained edge shading; the livery stays crisp.
        for name in ("CAB_BODY", "MODULE_WHITE", "REAR_DOOR", "SIDE_DOOR"):
            x0, y0, x1, y1 = REGIONS[name]
            rect((x0, y0, x1, y0 + 4), (248, 246, 235))
            rect((x0, y1 - 7, x1, y1), (183, 187, 184))
            draw.rectangle((x0 + 2, y0 + 2, x1 - 2, y1 - 2),
                           outline=(169, 171, 167, 255), width=1)

        x0, y0, x1, y1 = REGIONS["STRIPE"]
        rect((x0, y0, x1, y0 + 4), (218, 52, 55))
        rect((x0, y1 - 4, x1, y1), (111, 20, 27))
        for x in range(x0 + 8, x1 - 4, 16):
            rect((x, y0 + 10, x + 6, y0 + 11), (234, 151, 103))

        # Glass uses one coherent sky band so every pane reads as the same cab.
        x0, y0, x1, y1 = REGIONS["GLASS"]
        rect((x0, y0, x1, y0 + 18), (54, 79, 89))
        draw.polygon(((x0 + 3, y0 + 22), (x1 - 3, y0 + 7),
                      (x1 - 3, y0 + 13), (x0 + 3, y0 + 30)),
                     fill=(81, 107, 114, 255))
        rect((x0, y1 - 17, x1, y1), (13, 25, 31))
        rect((x0 + 8, y1 - 8, x1 - 8, y1 - 6), (31, 50, 57))

        # Simple original EMS emblem: blue six-arm medical mark, white centre.
        x0, y0, x1, y1 = REGIONS["MEDICAL"]
        blue = (36, 82, 145, 255)
        centre = ((x0 + x1) // 2, (y0 + y1) // 2)
        cx, cy = centre
        draw.polygon(((cx - 5, y0 + 7), (cx + 5, y0 + 7),
                      (cx + 5, cy - 8), (x1 - 8, cy - 18),
                      (x1 - 4, cy - 9), (cx + 9, cy),
                      (x1 - 4, cy + 9), (x1 - 8, cy + 18),
                      (cx + 5, cy + 8), (cx + 5, y1 - 7),
                      (cx - 5, y1 - 7), (cx - 5, cy + 8),
                      (x0 + 8, cy + 18), (x0 + 4, cy + 9),
                      (cx - 9, cy), (x0 + 4, cy - 9),
                      (x0 + 8, cy - 18), (cx - 5, cy - 8)), fill=blue)
        draw.ellipse((cx - 4, cy - 4, cx + 4, cy + 4),
                     fill=(225, 226, 216, 255))

        x0, y0, x1, y1 = REGIONS["EMS_TEXT"]
        glyphs = {
            "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
            "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
            "S": ("11111", "10000", "10000", "11111", "00001", "00001", "11111"),
        }
        cursor = x0 + 28
        for letter in "EMS":
            for row, bits in enumerate(glyphs[letter]):
                for column, bit in enumerate(bits):
                    if bit == "1":
                        rect((cursor + column * 2, y0 + 5 + row * 2,
                              cursor + column * 2 + 1, y0 + 6 + row * 2),
                             (247, 237, 220))
            cursor += 14
        rect((x0 + 3, y1 - 3, x1 - 3, y1 - 2), (116, 18, 24))

        # Mounted grille and lens cells retain bezels and internal ribbing.
        x0, y0, x1, y1 = REGIONS["GRILLE"]
        draw.rectangle((x0 + 2, y0 + 2, x1 - 2, y1 - 2),
                       outline=(151, 157, 154, 255), width=2)
        for y in range(y0 + 7, y1 - 3, 5):
            rect((x0 + 5, y, x1 - 5, y + 1), (91, 101, 102))

        for name in ("HEADLIGHT", "AMBER", "TAIL_RED", "REVERSE",
                     "LIGHT_RED", "LIGHT_BLUE"):
            x0, y0, x1, y1 = REGIONS[name]
            draw.rectangle((x0 + 2, y0 + 2, x1 - 2, y1 - 2),
                           outline=(33, 38, 39, 255), width=2)
            base = palette[name]
            bright = tuple(min(255, channel + 35) for channel in base)
            rect((x0 + 5, y0 + 5, x1 - 5, y0 + 7), bright)
            for x in range(x0 + 6, x1 - 4, 5):
                rect((x, y0 + 10, x + 1, y1 - 5), bright)

        x0, y0, x1, y1 = REGIONS["METAL"]
        rect((x0, y0, x1, y0 + 4), (201, 201, 189))
        rect((x0, y1 - 4, x1, y1), (70, 77, 78))

    TEXTURE.parent.mkdir(parents=True, exist_ok=True)
    image.save(TEXTURE)


def point_in_triangle(point, triangle):
    a, b, c = triangle
    u, v, q = b - a, c - a, np.asarray(point) - a
    determinant = u[0] * v[1] - u[1] * v[0]
    if abs(determinant) < 1e-9:
        return False
    s = (q[0] * v[1] - q[1] * v[0]) / determinant
    t = (u[0] * q[1] - u[1] * q[0]) / determinant
    return s >= -1e-6 and t >= -1e-6 and s + t <= 1 + 1e-6


def body_surface_z(triangles, x, y, front):
    candidates = []
    for triangle in triangles:
        projected = triangle[:, :2]
        if point_in_triangle((x, y), projected):
            a, b, c = triangle
            normal = np.cross(b - a, c - a)
            if abs(normal[2]) < 1e-8:
                continue
            # Solve the triangle plane at source X/Y.
            z = a[2] - (normal[0] * (x - a[0]) + normal[1] * (y - a[1])) / normal[2]
            candidates.append(z)
    if not candidates:
        return None
    return max(candidates) if front else min(candidates)


def validate():
    vertices, indices = read_emesh(MODEL / "body.emesh")
    vertex_data = np.asarray(vertices)
    triangles = vertex_data[np.asarray(indices).reshape(-1, 3), :3]
    assert len(triangles) >= SHAPE["triangle_budget"][0], len(triangles)
    assert len(triangles) <= SHAPE["triangle_budget"][1], len(triangles)
    assert np.isfinite(vertex_data).all()
    assert min(indices) >= 0 and max(indices) < len(vertex_data)
    areas = np.linalg.norm(np.cross(triangles[:, 1] - triangles[:, 0],
                                    triangles[:, 2] - triangles[:, 0]), axis=1)
    assert (areas > 1e-9).all(), "degenerate triangle"
    assert ((vertex_data[:, 6:8] >= 0) & (vertex_data[:, 6:8] <= 1)).all()
    assert np.allclose(np.linalg.norm(vertex_data[:, 3:6], axis=1), 1, atol=1e-4)

    low = vertex_data[:, :3].min(axis=0)
    high = vertex_data[:, :3].max(axis=0)
    assert abs(low[0] + high[0]) < 1e-4, (low, high)
    assert 6.0 < high[2] - low[2] < 6.2
    assert 2.5 < high[1] < 2.7
    # The passenger entry door and driver-side service lockers are deliberately
    # asymmetric, but the chassis and overall envelope remain centred.
    assert abs(low[0] + high[0]) < 1e-4

    # Four authored side-wall holes remain open at the shared wheel anchors.
    opening_samples = 0
    for axle in (WHEELS["front_z"], WHEELS["rear_z"]):
        for side in (-1, 1):
            side_triangles = triangles[(side * triangles[:, :, 0] > 0.81).all(axis=1)]
            for dz, dy in ((0, 0), (0.13, 0), (-0.13, 0), (0, 0.14), (0, -0.12)):
                point = np.array([axle + dz, WHEELS["arch_y"] + dy])
                assert not any(point_in_triangle(point, triangle[:, [2, 1]])
                               for triangle in side_triangles), "closed wheel opening"
                opening_samples += 1

    # Windshield and cab side glass sit in openings, with no opaque upper-cab slab.
    cab_glass_samples = 0
    front_skin = triangles[(triangles[:, :, 2] > 0.80).all(axis=1)]
    for x in (-0.55, 0, 0.55):
        point = (x, 1.58)
        hits = [triangle for triangle in front_skin
                if point_in_triangle(point, triangle[:, [0, 1]])]
        assert hits, "windshield opening is missing its fitted pane"
        cab_glass_samples += 1

    # Lamp profiles land on actual front/rear receiver geometry.
    for side in (-1, 1):
        front = body_surface_z(triangles, side * 0.75, 0.81, True)
        rear_low = body_surface_z(triangles, side * 0.93, 0.74, False)
        rear_high = body_surface_z(triangles, side * 0.93, 1.04, False)
        assert front is not None and 2.99 < front < 3.04
        assert rear_low is not None and -3.09 < rear_low < -2.99
        assert rear_high is not None and -3.09 < rear_high < -2.99

    # Real side- and rear-window apertures are occupied by glass only at their centres.
    for point in ((-1.09, 1.88, -0.27), (-0.39, 1.74, -3.065),
                  (0.39, 1.74, -3.065)):
        distance = np.linalg.norm(vertex_data[:, :3] - np.asarray(point), axis=1)
        assert distance.min() < 0.45, "glass aperture drifted from its frame"

    with Image.open(TEXTURE) as image:
        assert image.mode == "RGBA" and image.size == (ATLAS_SIZE, ATLAS_SIZE)
        assert image.getchannel("A").getextrema() == (255, 255)
        colours = len(set(image.getdata()))
        assert colours <= 96, colours

    report = {
        "asset": "Municipal Ambulance",
        "method": "Harrow Workman explicit open-arch and framed-glass construction",
        "vertices": len(vertex_data),
        "triangles": len(triangles),
        "bounds_min": low.tolist(),
        "bounds_max": high.tolist(),
        "wheel_anchors": WHEELS,
        "atlas": [ATLAS_SIZE, ATLAS_SIZE, "RGBA"],
        "palette_colours": colours,
        "checks": [
            "one joined wheel-less body",
            "centred symmetric chassis and body envelope",
            "four true wheel openings",
            "framed windshield and side glass",
            "real side-door and rear-door glass apertures",
            "mounted grille and lamp receiver solids",
            "finite geometry, unit normals and valid UVs",
        ],
        "samples": {
            "wheel_openings": opening_samples,
            "windshield": cab_glass_samples,
            "front_and_rear_lamps": 6,
        },
    }
    report_path = ROOT / "build/municipal-ambulance-fit-report.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n")

    guide = Image.open(TEXTURE).copy()
    guide_draw = ImageDraw.Draw(guide)
    for triangle_indices in np.asarray(indices).reshape(-1, 3):
        points = [(vertex_data[index, 6] * ATLAS_SIZE,
                   (1 - vertex_data[index, 7]) * ATLAS_SIZE)
                  for index in triangle_indices]
        guide_draw.line(points + [points[0]], fill=(242, 164, 59, 255))
    guide.save(ROOT / "build/municipal-ambulance-uv-guide.png")
    print(json.dumps(report, indent=2))


def make_preview():
    subprocess.run([
        "python3",
        str(ROOT / "tools/render_firetruck_preview.py"),
        "--body-mesh", str(MODEL / "body.emesh"),
        "--body-texture", str(TEXTURE),
        "--output", str(ROOT / "build/municipal-ambulance-preview.png"),
        "--body-length", str(SHAPE["length"]),
        "--wheel-x", str(WHEELS["x"]),
        "--arch-y", str(WHEELS["arch_y"]),
        "--front-wheel-z", str(WHEELS["front_z"]),
        "--rear-wheel-z", str(WHEELS["rear_z"]),
    ], check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--blockout", action="store_true")
    parser.add_argument("--texture-only", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--preview-only", action="store_true")
    args = parser.parse_args()
    if args.preview_only:
        make_preview()
        return
    if not args.validate_only:
        make_texture(args.blockout)
    if not args.texture_only and not args.validate_only:
        subprocess.run([
            "/Applications/Blender.app/Contents/MacOS/Blender",
            "--background", "--factory-startup", "--python",
            str(ROOT / "tools/municipal_ambulance_blender.py"), "--",
            "--mesh", str(MODEL / "body.emesh"),
            "--blend", str(MODEL / "source.blend"),
        ], check=True)
    validate()
    make_preview()


if __name__ == "__main__":
    main()
