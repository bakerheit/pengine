#!/usr/bin/env python3
"""Cook the original orange GLR ZIP body and hand-painted PSX atlas."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

from glr_zip_spec import ATLAS_SIZE, REGIONS
from make_vesper_vx91_assets import read_emesh


ROOT = Path(__file__).resolve().parents[1]
BLENDER = Path("/Applications/Blender.app/Contents/MacOS/Blender")

TARGET_WIDTH_RANGE = (2.15, 2.45)
TARGET_HEIGHT_RANGE = (1.08, 1.20)
TARGET_LENGTH_RANGE = (4.95, 5.15)
WHEEL_ANCHORS = {
    "front_z": 1.65,
    "rear_z": -1.60,
    "x": 0.98,
    "arch_y": 0.46,
    "inner_radius": 0.50,
}


def make_texture(output: Path) -> None:
    """Paint a clean atlas with broad, surface-aware PSX colour bands."""
    image = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (15, 10, 9, 255))
    draw = ImageDraw.Draw(image)

    def inset(box: tuple[int, int, int, int], amount: int):
        x0, y0, x1, y1 = box
        return x0 + amount, y0 + amount, x1 - amount, y1 - amount

    def horizontal_steps(box: tuple[int, int, int, int],
                         colours: tuple[tuple[int, int, int, int], ...]) -> None:
        x0, y0, x1, y1 = box
        height = y1 - y0 + 1
        for index, colour in enumerate(colours):
            top = y0 + round(height * index / len(colours))
            bottom = y0 + round(height * (index + 1) / len(colours)) - 1
            draw.rectangle((x0, top, x1, bottom), fill=colour)

    # Clean tangerine paint with broad value ramps. These bands map across
    # neighbouring faces, so the low-poly facets stay readable without noise.
    horizontal_steps(REGIONS["BODY_SIDE"], (
        (255, 137, 43, 255), (250, 104, 27, 255),
        (239, 76, 20, 255), (211, 53, 18, 255),
        (153, 34, 18, 255),
    ))
    x0, y0, x1, y1 = REGIONS["BODY_SIDE"]
    draw.polygon(((x0 + 8, y0 + 17), (x1 - 10, y0 + 7),
                  (x1 - 10, y0 + 13), (x0 + 8, y0 + 25)),
                 fill=(255, 164, 67, 255))
    draw.polygon(((x0 + 12, y0 + 40), (x1 - 22, y0 + 34),
                  (x1 - 22, y0 + 38), (x0 + 12, y0 + 47)),
                 fill=(245, 91, 23, 255))
    draw.rectangle((x0 + 6, y1 - 11, x1 - 5, y1 - 7),
                   fill=(119, 29, 21, 255))

    draw.rectangle(REGIONS["BODY_TOP"], fill=(247, 91, 23, 255))
    x0, y0, x1, y1 = REGIONS["BODY_TOP"]
    draw.polygon(((x0 + 8, y0 + 9), (x1 - 9, y0 + 17),
                  (x1 - 9, y0 + 29), (x0 + 8, y0 + 21)),
                 fill=(255, 151, 49, 255))
    draw.rectangle((x0 + 18, y0 + 33, x1 - 18, y0 + 38),
                   fill=(255, 121, 31, 255))
    draw.rectangle((x0 + 7, y1 - 18, x1 - 7, y1 - 7),
                   fill=(197, 45, 18, 255))

    horizontal_steps(REGIONS["BODY_SHADOW"], (
        (195, 48, 17, 255), (157, 34, 18, 255),
        (105, 26, 22, 255),
    ))

    # Glass has one deliberate sky streak instead of random mottling.
    horizontal_steps(REGIONS["GLASS"], (
        (38, 68, 76, 255), (20, 43, 54, 255),
        (8, 22, 31, 255), (5, 12, 19, 255),
    ))
    x0, y0, x1, y1 = REGIONS["GLASS"]
    draw.polygon(((x0 + 5, y0 + 10), (x1 - 7, y0 + 5),
                  (x1 - 7, y0 + 10), (x0 + 5, y0 + 18)),
                 fill=(65, 100, 105, 255))

    horizontal_steps(REGIONS["CLADDING"], (
        (54, 49, 46, 255), (31, 29, 29, 255), (12, 13, 15, 255),
    ))
    draw.rectangle(REGIONS["SEAM"], fill=(9, 7, 8, 255))

    # Lamps use framed, bright centres so the small geometry reads at traffic
    # distance. Each region remains crisp with nearest-neighbour sampling.
    lamp_colours = {
        "TAIL_RED": ((72, 5, 12, 255), (246, 37, 35, 255),
                     (255, 96, 48, 255)),
        "TAIL_AMBER": ((91, 23, 3, 255), (255, 124, 9, 255),
                       (255, 189, 49, 255)),
        "REVERSE": ((70, 80, 82, 255), (211, 217, 202, 255),
                    (255, 244, 210, 255)),
        "MARKER_AMBER": ((102, 29, 3, 255), (245, 116, 8, 255),
                         (255, 181, 35, 255)),
        "HEADLIGHT": ((80, 84, 77, 255), (223, 221, 193, 255),
                      (255, 251, 217, 255)),
    }
    for key, (frame, lens, glint) in lamp_colours.items():
        box = REGIONS[key]
        draw.rectangle(box, fill=frame)
        draw.rectangle(inset(box, 3), fill=lens)
        bx0, by0, bx1, by1 = inset(box, 6)
        draw.rectangle((bx0, by0, bx1, min(by1, by0 + 3)), fill=glint)

    horizontal_steps(REGIONS["METAL"], (
        (218, 210, 190, 255), (123, 121, 117, 255),
        (57, 61, 66, 255),
    ))
    horizontal_steps(REGIONS["INTERIOR"], (
        (202, 195, 181, 255), (145, 139, 133, 255),
        (83, 80, 82, 255),
    ))
    horizontal_steps(REGIONS["EXHAUST"], (
        (113, 104, 91, 255), (50, 50, 51, 255), (7, 8, 10, 255),
    ))
    horizontal_steps(REGIONS["LENS_DARK"], (
        (29, 47, 56, 255), (8, 16, 23, 255), (2, 4, 7, 255),
    ))
    horizontal_steps(REGIONS["GRIME"], (
        (99, 65, 43, 255), (56, 43, 36, 255), (29, 28, 28, 255),
    ))
    horizontal_steps(REGIONS["DASH"], (
        (56, 49, 48, 255), (28, 26, 28, 255), (8, 10, 13, 255),
    ))
    draw.rectangle(REGIONS["BLACK"], fill=(3, 4, 6, 255))

    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output, optimize=True)


def make_uv_guide(mesh_path: Path, output: Path) -> None:
    vertices, indices = read_emesh(mesh_path)
    guide = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (17, 12, 12, 255))
    draw = ImageDraw.Draw(guide)
    for name, box in REGIONS.items():
        draw.rectangle(box, outline=(113, 68, 53, 255))
        draw.text((box[0] + 2, box[1] + 2), name[:11],
                  fill=(236, 182, 143, 255))
    for start in range(0, len(indices), 3):
        points = []
        for index in indices[start:start + 3]:
            u, v = vertices[index][6:8]
            points.append((round(u * 255), round((1.0 - v) * 255)))
        draw.line((*points, points[0]), fill=(255, 139, 63, 120), width=1)
    output.parent.mkdir(parents=True, exist_ok=True)
    guide.save(output)


def validate(mesh_path: Path, texture_path: Path, report_path: Path) -> dict:
    vertices, indices = read_emesh(mesh_path)
    if not vertices or not indices or len(indices) % 3:
        raise ValueError("GLR ZIP mesh is empty or not triangulated")
    if any(index >= len(vertices) for index in indices):
        raise ValueError("out-of-range mesh index")
    if any(not math.isfinite(value) for vertex in vertices for value in vertex):
        raise ValueError("mesh contains a non-finite vertex value")

    positions = [vertex[:3] for vertex in vertices]
    mins = [min(point[axis] for point in positions) for axis in range(3)]
    maxs = [max(point[axis] for point in positions) for axis in range(3)]
    sizes = [maxs[axis] - mins[axis] for axis in range(3)]
    triangles = len(indices) // 3
    if not 650 <= triangles <= 1300:
        raise ValueError(f"triangle budget missed: {triangles}")
    if any(not 0.0 <= value <= 1.0 for vertex in vertices
           for value in vertex[6:8]):
        raise ValueError("UV outside atlas")
    if abs(mins[0] + maxs[0]) > 0.04 or abs(mins[2] + maxs[2]) > 0.04:
        raise ValueError(f"body is not centered on XZ: {mins} to {maxs}")
    if not TARGET_WIDTH_RANGE[0] <= sizes[0] <= TARGET_WIDTH_RANGE[1]:
        raise ValueError(f"GLR ZIP width is outside target range: {sizes[0]:.4f}")
    if not TARGET_HEIGHT_RANGE[0] <= sizes[1] <= TARGET_HEIGHT_RANGE[1]:
        raise ValueError(f"GLR ZIP height is outside target range: {sizes[1]:.4f}")
    if not TARGET_LENGTH_RANGE[0] <= sizes[2] <= TARGET_LENGTH_RANGE[1]:
        raise ValueError(f"GLR ZIP length is outside target range: {sizes[2]:.4f}")
    if mins[1] < -0.05:
        raise ValueError(f"GLR ZIP body drops below its ground datum: {mins[1]:.4f}")

    def projected_triangle_contains(point, triangle) -> bool:
        def signed(a, b, c):
            return ((a[0] - c[0]) * (b[1] - c[1]) -
                    (b[0] - c[0]) * (a[1] - c[1]))
        d0 = signed(point, triangle[0], triangle[1])
        d1 = signed(point, triangle[1], triangle[2])
        d2 = signed(point, triangle[2], triangle[0])
        return ((d0 > 1e-6 and d1 > 1e-6 and d2 > 1e-6) or
                (d0 < -1e-6 and d1 < -1e-6 and d2 < -1e-6))

    # A decorative arch over a solid side panel caused the shared front wheel
    # to cut through orange bodywork at full lock. Prove each axle centre is
    # genuinely open when viewed through either body side.
    sample_offsets = ((0.0, 0.0), (-0.05, 0.0), (0.05, 0.0),
                      (0.0, -0.05), (0.0, 0.05))
    for wheel_z in (WHEEL_ANCHORS["front_z"], WHEEL_ANCHORS["rear_z"]):
        for side in (-1.0, 1.0):
            for z_offset, y_offset in sample_offsets:
                sample = (wheel_z + z_offset,
                          WHEEL_ANCHORS["arch_y"] + y_offset)
                for start in range(0, len(indices), 3):
                    triangle_3d = [positions[indices[start + offset]]
                                   for offset in range(3)]
                    if not all(side * vertex[0] > 0.75
                               for vertex in triangle_3d):
                        continue
                    triangle_2d = [(vertex[2], vertex[1])
                                   for vertex in triangle_3d]
                    if projected_triangle_contains(sample, triangle_2d):
                        raise ValueError(
                            "solid side panel covers wheel well near "
                            f"z={wheel_z} at sample={sample}"
                        )

    # The wheel pockets must stop before the centreline. A full-width tunnel
    # also passes the side-opening test, but deletes the hood over the front
    # axle. Check just ahead of and behind that triangulation seam.
    for hood_x in (-0.60, 0.0, 0.60):
        for hood_z in (WHEEL_ANCHORS["front_z"] - 0.05,
                       WHEEL_ANCHORS["front_z"] + 0.05):
            hood_present = False
            for start in range(0, len(indices), 3):
                triangle_3d = [positions[indices[start + offset]]
                               for offset in range(3)]
                if not all(vertex[1] > 0.65 for vertex in triangle_3d):
                    continue
                triangle_2d = [(vertex[0], vertex[2])
                               for vertex in triangle_3d]
                if projected_triangle_contains((hood_x, hood_z), triangle_2d):
                    hood_present = True
                    break
            if not hood_present:
                raise ValueError(
                    "front hood is missing or too narrow near "
                    f"x={hood_x}, z={hood_z}"
                )

    with Image.open(texture_path) as texture:
        if texture.size != (ATLAS_SIZE, ATLAS_SIZE) or texture.mode != "RGBA":
            raise ValueError("texture must be a 256x256 RGBA atlas")
        if texture.getchannel("A").getextrema() != (255, 255):
            raise ValueError("GLR ZIP atlas must be fully opaque")
        palette_colours = len(set(texture.getdata()))
    if palette_colours > 96:
        raise ValueError(
            f"atlas is too smooth for PS1 art: {palette_colours} colours"
        )

    report = {
        "asset": "GLR ZIP",
        "mesh": str(mesh_path),
        "texture": str(texture_path),
        "vertices": len(vertices),
        "triangles": triangles,
        "bounds_min": [round(value, 5) for value in mins],
        "bounds_max": [round(value, 5) for value in maxs],
        "bounds_size": [round(value, 5) for value in sizes],
        "atlas": [ATLAS_SIZE, ATLAS_SIZE, "RGBA"],
        "palette_colours": palette_colours,
        "alpha": "opaque",
        "uv_range": [
            round(min(vertex[6] for vertex in vertices), 5),
            round(min(vertex[7] for vertex in vertices), 5),
            round(max(vertex[6] for vertex in vertices), 5),
            round(max(vertex[7] for vertex in vertices), 5),
        ],
        "wheel_anchors": WHEEL_ANCHORS,
        "art_direction": {
            "paint": "clean tangerine with broad mango highlights and burnt-orange shading",
            "contrast": "warm graphite targa, intakes, and bumpers",
            "front": "four slim inset lamps in two recessed black pods",
            "side": "broad inward-sloping fender crowns and a clean shoulder crease",
            "cabin": "light-gray bucket seats and a charcoal dashboard",
            "rear": "three vertical red cells per outer corner and a body-colour centre",
            "deck": "broad engine-cover panels",
            "exhaust": "one wide trapezoid outlet",
            "finish": "clean 3-8 px highlight bands with restrained lower-body texture",
        },
        "checks": [
            "single joined BODY mesh",
            "no wheel geometry",
            "four open arches with restrained upper lips",
            "side projection is open at all four axle centres",
            "wide centre hood preserved across the front wheel pockets",
            "mirrored body geometry",
            "semantic atlas regions",
            "flat normals",
            "player fit preserved",
        ],
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--blender", type=Path, default=BLENDER)
    parser.add_argument(
        "--texture", type=Path,
        default=ROOT / "assets/textures/vehicles/glr_zip/body.png",
    )
    parser.add_argument(
        "--mesh", type=Path,
        default=ROOT / "assets/models/vehicles/glr_zip/body.emesh",
    )
    parser.add_argument(
        "--blend", type=Path,
        default=ROOT / "assets/models/vehicles/glr_zip/source.blend",
    )
    parser.add_argument(
        "--report", type=Path,
        default=ROOT / "build/glr-zip-fit-report.json",
    )
    parser.add_argument(
        "--uv-guide", type=Path,
        default=ROOT / "build/glr-zip-uv-guide.png",
    )
    args = parser.parse_args()

    if not args.blender.is_file():
        raise SystemExit(f"Blender not found: {args.blender}")
    blender_script = ROOT / "tools/glr_zip_blender.py"
    if not blender_script.is_file():
        raise SystemExit(f"GLR ZIP Blender builder not found: {blender_script}")

    make_texture(args.texture)
    subprocess.run([
        str(args.blender), "--background", "--factory-startup", "--python",
        str(blender_script), "--",
        "--mesh", str(args.mesh), "--blend", str(args.blend),
    ], cwd=ROOT, check=True)
    make_uv_guide(args.mesh, args.uv_guide)
    report = validate(args.mesh, args.texture, args.report)
    print(
        f"GLR_ZIP_COOK mesh={args.mesh} triangles={report['triangles']} "
        f"vertices={report['vertices']} atlas={args.texture} "
        f"blend={args.blend} report={args.report}"
    )


    from bake_vehicle_surfaces import bake_if_canonical
    bake_if_canonical("glr_zip", args.mesh, args.texture)


if __name__ == "__main__":
    main()
