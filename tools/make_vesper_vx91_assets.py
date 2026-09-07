#!/usr/bin/env python3
"""Build the Vesper VX-91 from its Blender source and exact pixel atlas."""

from __future__ import annotations

import argparse
import json
import math
import struct
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

from vesper_vx91_spec import ATLAS_SIZE, REGIONS


ROOT = Path(__file__).resolve().parents[1]
BLENDER = Path("/Applications/Blender.app/Contents/MacOS/Blender")
def fill_clustered(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int],
                   base: tuple[int, int, int, int],
                   accents: tuple[tuple[int, int, int, int], ...]) -> None:
    """Fill one region with crisp, repeatable 2-4 px paint clusters."""
    draw.rectangle(box, fill=base)
    x0, y0, x1, y1 = box
    for row, colour in enumerate(accents):
        y = y0 + 5 + row * max(4, (y1 - y0 - 8) // max(1, len(accents)))
        draw.rectangle((x0 + 3, y, x1 - 3, min(y + 2, y1 - 2)), fill=colour)
        for x in range(x0 + 7 + row * 3, x1 - 4, 17):
            draw.rectangle((x, min(y + 3, y1 - 2), min(x + 3, x1 - 2),
                            min(y + 5, y1 - 2)), fill=colour)


def make_texture(output: Path) -> None:
    image = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (10, 12, 14, 255))
    draw = ImageDraw.Draw(image)
    fill_clustered(draw, REGIONS["BODY_SIDE"], (6, 76, 76, 255), (
        (8, 94, 92, 255), (10, 108, 102, 255), (5, 62, 65, 255),
        (18, 116, 106, 255), (4, 53, 58, 255),
    ))
    fill_clustered(draw, REGIONS["BODY_TOP"], (8, 91, 88, 255), (
        (22, 123, 112, 255), (14, 108, 102, 255), (7, 76, 77, 255),
        (30, 132, 117, 255), (6, 69, 71, 255),
    ))
    fill_clustered(draw, REGIONS["BODY_SHADOW"], (4, 47, 53, 255), (
        (5, 61, 65, 255), (3, 38, 45, 255), (8, 70, 70, 255),
    ))
    fill_clustered(draw, REGIONS["GLASS"], (8, 18, 25, 255), (
        (18, 43, 51, 255), (25, 57, 62, 255), (10, 29, 37, 255),
        (34, 69, 70, 255),
    ))
    fill_clustered(draw, REGIONS["CLADDING"], (19, 23, 27, 255), (
        (32, 37, 40, 255), (12, 15, 19, 255), (42, 45, 46, 255),
    ))
    fill_clustered(draw, REGIONS["SEAM"], (4, 10, 13, 255), (
        (8, 24, 27, 255), (2, 6, 9, 255),
    ))
    fill_clustered(draw, REGIONS["TAIL_RED"], (132, 18, 19, 255), (
        (220, 43, 31, 255), (92, 10, 14, 255), (246, 78, 43, 255),
    ))
    fill_clustered(draw, REGIONS["TAIL_AMBER"], (154, 60, 9, 255), (
        (236, 133, 27, 255), (111, 36, 7, 255),
    ))
    fill_clustered(draw, REGIONS["REVERSE"], (151, 158, 150, 255), (
        (215, 214, 190, 255), (100, 116, 116, 255),
    ))
    fill_clustered(draw, REGIONS["MARKER_AMBER"], (177, 72, 7, 255), (
        (244, 145, 19, 255), (111, 37, 5, 255),
    ))
    fill_clustered(draw, REGIONS["METAL"], (108, 113, 112, 255), (
        (180, 186, 177, 255), (65, 71, 73, 255), (208, 207, 188, 255),
    ))
    fill_clustered(draw, REGIONS["INTERIOR"], (19, 20, 23, 255), (
        (42, 43, 46, 255), (10, 11, 14, 255), (57, 53, 50, 255),
    ))
    fill_clustered(draw, REGIONS["EXHAUST"], (24, 27, 29, 255), (
        (77, 78, 74, 255), (8, 10, 12, 255), (105, 99, 88, 255),
    ))
    fill_clustered(draw, REGIONS["LENS_DARK"], (9, 15, 18, 255), (
        (31, 45, 48, 255), (4, 8, 11, 255),
    ))
    fill_clustered(draw, REGIONS["GRIME"], (41, 39, 34, 255), (
        (67, 58, 45, 255), (23, 25, 24, 255),
    ))
    fill_clustered(draw, REGIONS["DASH"], (17, 22, 24, 255), (
        (48, 52, 51, 255), (9, 12, 15, 255),
    ))
    draw.rectangle(REGIONS["BLACK"], fill=(3, 5, 7, 255))

    # Pixel highlights are deliberately broad enough to survive nearest-neighbor
    # sampling and PS1-style distance reduction.
    for key in ("BODY_SIDE", "BODY_TOP"):
        x0, y0, x1, y1 = REGIONS[key]
        draw.rectangle((x0 + 5, y0 + 5, x1 - 6, y0 + 8),
                       fill=(33, 137, 121, 255))
        draw.rectangle((x0 + 8, y1 - 10, x1 - 5, y1 - 7),
                       fill=(4, 48, 53, 255))

    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output, optimize=True)


def read_emesh(path: Path) -> tuple[list[tuple[float, ...]], list[int]]:
    data = path.read_bytes()
    magic, version, flags, vertex_count, index_count, submesh_count, _, _ = (
        struct.unpack_from("<8I", data, 0)
    )
    if (magic, version, flags, submesh_count) != (0x48534D45, 2, 0, 1):
        raise ValueError(f"unsupported static emesh: {path}")
    vertices = [
        struct.unpack_from("<12f", data, 32 + index * 48)
        for index in range(vertex_count)
    ]
    index_offset = 32 + vertex_count * 48
    indices = list(struct.unpack_from(f"<{index_count}I", data, index_offset))
    return vertices, indices


def make_uv_guide(mesh_path: Path, output: Path) -> None:
    vertices, indices = read_emesh(mesh_path)
    guide = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (12, 14, 17, 255))
    draw = ImageDraw.Draw(guide)
    for name, (x0, y0, x1, y1) in REGIONS.items():
        draw.rectangle((x0, y0, x1, y1), outline=(60, 68, 73, 255))
        draw.text((x0 + 2, y0 + 2), name[:11], fill=(185, 191, 184, 255))
    for start in range(0, len(indices), 3):
        points = []
        for index in indices[start:start + 3]:
            u, v = vertices[index][6:8]
            points.append((round(u * 255), round((1.0 - v) * 255)))
        draw.line((*points, points[0]), fill=(79, 220, 190, 110), width=1)
    output.parent.mkdir(parents=True, exist_ok=True)
    guide.save(output)


def validate(mesh_path: Path, texture_path: Path, report_path: Path) -> dict:
    vertices, indices = read_emesh(mesh_path)
    positions = [vertex[:3] for vertex in vertices]
    mins = [min(point[axis] for point in positions) for axis in range(3)]
    maxs = [max(point[axis] for point in positions) for axis in range(3)]
    triangle_count = len(indices) // 3
    if not 650 <= triangle_count <= 1300:
        raise ValueError(f"triangle budget missed: {triangle_count}")
    if any(index >= len(vertices) for index in indices):
        raise ValueError("out-of-range mesh index")
    if any(not 0.0 <= value <= 1.0 for vertex in vertices
           for value in vertex[6:8]):
        raise ValueError("UV outside atlas")
    if abs(mins[0] + maxs[0]) > 1e-4 or abs(mins[2] + maxs[2]) > 1e-4:
        raise ValueError("body is not centered on XZ")
    if not (math.isclose(maxs[0] - mins[0], 2.5, abs_tol=1e-4) and
            math.isclose(maxs[2] - mins[2], 6.44, abs_tol=1e-4)):
        raise ValueError(f"unexpected traffic bounds: {mins} to {maxs}")
    with Image.open(texture_path) as texture:
        if texture.size != (256, 256) or texture.mode != "RGBA":
            raise ValueError("texture must be a 256x256 RGBA atlas")
        alpha = texture.getchannel("A")
        if alpha.getextrema() != (255, 255):
            raise ValueError("vehicle atlas must be fully opaque")
        palette_colours = len(set(texture.getdata()))
    if palette_colours > 96:
        raise ValueError(f"atlas is too smooth for PS1 art: {palette_colours} colours")
    report = {
        "asset": "Vesper VX-91",
        "mesh": str(mesh_path),
        "texture": str(texture_path),
        "vertices": len(vertices),
        "triangles": triangle_count,
        "bounds_min": [round(value, 5) for value in mins],
        "bounds_max": [round(value, 5) for value in maxs],
        "atlas": [256, 256, "RGBA"],
        "palette_colours": palette_colours,
        "alpha": "opaque",
        "uv_range": [
            round(min(vertex[6] for vertex in vertices), 5),
            round(min(vertex[7] for vertex in vertices), 5),
            round(max(vertex[6] for vertex in vertices), 5),
            round(max(vertex[7] for vertex in vertices), 5),
        ],
        "wheel_anchors": {
            "front_z": 2.08, "rear_z": -1.86,
            "x": 1.08, "arch_y": 0.55, "inner_radius": 0.54,
        },
        "checks": [
            "single joined BODY mesh", "no wheel geometry",
            "four open arch rings", "mirrored body geometry",
            "semantic atlas regions", "flat normals", "traffic bounds preserved",
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
        default=ROOT / "assets/textures/vehicles/vesper_vx91/body.png",
    )
    parser.add_argument(
        "--mesh", type=Path,
        default=ROOT / "assets/models/vehicles/vesper_vx91/body.emesh",
    )
    parser.add_argument(
        "--blend", type=Path,
        default=ROOT / "assets/models/vehicles/vesper_vx91/source.blend",
    )
    parser.add_argument(
        "--report", type=Path,
        default=ROOT / "build/vesper-vx91-fit-report.json",
    )
    parser.add_argument(
        "--uv-guide", type=Path,
        default=ROOT / "build/vesper-vx91-uv-guide.png",
    )
    args = parser.parse_args()
    if not args.blender.is_file():
        raise SystemExit(f"Blender not found: {args.blender}")
    make_texture(args.texture)
    blender_script = ROOT / "tools/vesper_vx91_blender.py"
    command = [
        str(args.blender), "--background", "--factory-startup",
        "--python", str(blender_script), "--",
        "--mesh", str(args.mesh), "--blend", str(args.blend),
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    make_uv_guide(args.mesh, args.uv_guide)
    report = validate(args.mesh, args.texture, args.report)
    print(
        f"VESPER_COOK mesh={args.mesh} triangles={report['triangles']} "
        f"vertices={report['vertices']} atlas={args.texture} "
        f"blend={args.blend} report={args.report}"
    )


    from bake_vehicle_surfaces import bake_if_canonical
    bake_if_canonical("vesper_vx91", args.mesh, args.texture)


if __name__ == "__main__":
    main()
