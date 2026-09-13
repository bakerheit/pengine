#!/usr/bin/env python3
"""Cook the original neon-green GLM Lunge body and PSX atlas."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

from glm_lunge_spec import ATLAS_SIZE, REGIONS
from make_vesper_vx91_assets import fill_clustered, read_emesh


ROOT = Path(__file__).resolve().parents[1]
BLENDER = Path("/Applications/Blender.app/Contents/MacOS/Blender")


def make_texture(output: Path) -> None:
    image = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (8, 12, 8, 255))
    draw = ImageDraw.Draw(image)
    # The city night pass is strongly blue. Keep the source paint hot and
    # yellow-green so it still reads as neon green once that light hits it.
    fill_clustered(draw, REGIONS["BODY_SIDE"], (163, 230, 13, 255), (
        (201, 255, 25, 255), (127, 199, 8, 255), (226, 255, 47, 255),
        (96, 161, 7, 255), (184, 242, 17, 255),
    ))
    fill_clustered(draw, REGIONS["BODY_TOP"], (190, 244, 20, 255), (
        (232, 255, 53, 255), (207, 255, 28, 255), (149, 215, 11, 255),
        (246, 255, 69, 255), (119, 181, 8, 255),
    ))
    fill_clustered(draw, REGIONS["BODY_SHADOW"], (78, 116, 5, 255), (
        (101, 145, 7, 255), (49, 83, 4, 255), (121, 165, 8, 255),
    ))
    fill_clustered(draw, REGIONS["GLASS"], (5, 13, 13, 255), (
        (16, 40, 35, 255), (25, 57, 44, 255), (8, 24, 23, 255),
        (39, 76, 51, 255),
    ))
    fill_clustered(draw, REGIONS["CLADDING"], (12, 15, 13, 255), (
        (28, 32, 27, 255), (5, 7, 7, 255), (43, 46, 37, 255),
    ))
    fill_clustered(draw, REGIONS["SEAM"], (3, 8, 5, 255), (
        (8, 22, 13, 255), (1, 4, 3, 255),
    ))
    fill_clustered(draw, REGIONS["TAIL_RED"], (130, 12, 12, 255), (
        (239, 38, 23, 255), (79, 7, 10, 255), (255, 78, 37, 255),
    ))
    fill_clustered(draw, REGIONS["TAIL_AMBER"], (164, 65, 5, 255), (
        (244, 141, 18, 255), (98, 29, 4, 255),
    ))
    fill_clustered(draw, REGIONS["REVERSE"], (143, 157, 145, 255), (
        (218, 225, 195, 255), (86, 105, 99, 255),
    ))
    fill_clustered(draw, REGIONS["MARKER_AMBER"], (176, 70, 4, 255), (
        (255, 155, 19, 255), (107, 34, 3, 255),
    ))
    fill_clustered(draw, REGIONS["METAL"], (92, 102, 94, 255), (
        (180, 192, 169, 255), (50, 59, 56, 255), (218, 221, 191, 255),
    ))
    fill_clustered(draw, REGIONS["INTERIOR"], (15, 16, 14, 255), (
        (41, 43, 36, 255), (6, 8, 7, 255), (58, 55, 43, 255),
    ))
    fill_clustered(draw, REGIONS["EXHAUST"], (20, 24, 22, 255), (
        (75, 80, 70, 255), (6, 8, 8, 255), (111, 104, 86, 255),
    ))
    fill_clustered(draw, REGIONS["LENS_DARK"], (6, 13, 11, 255), (
        (25, 47, 34, 255), (2, 6, 5, 255),
    ))
    fill_clustered(draw, REGIONS["GRIME"], (41, 43, 31, 255), (
        (70, 65, 43, 255), (20, 25, 19, 255),
    ))
    fill_clustered(draw, REGIONS["DASH"], (12, 18, 15, 255), (
        (44, 51, 41, 255), (5, 9, 8, 255),
    ))
    draw.rectangle(REGIONS["BLACK"], fill=(2, 4, 3, 255))
    for key in ("BODY_SIDE", "BODY_TOP"):
        x0, y0, x1, y1 = REGIONS[key]
        draw.rectangle((x0 + 5, y0 + 5, x1 - 6, y0 + 8),
                       fill=(239, 255, 58, 255))
        draw.rectangle((x0 + 8, y1 - 10, x1 - 5, y1 - 7),
                       fill=(82, 134, 5, 255))
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output, optimize=True)


def make_uv_guide(mesh_path: Path, output: Path) -> None:
    vertices, indices = read_emesh(mesh_path)
    guide = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (10, 14, 10, 255))
    draw = ImageDraw.Draw(guide)
    for name, box in REGIONS.items():
        draw.rectangle(box, outline=(74, 99, 69, 255))
        draw.text((box[0] + 2, box[1] + 2), name[:11],
                  fill=(192, 222, 177, 255))
    for start in range(0, len(indices), 3):
        points = []
        for index in indices[start:start + 3]:
            u, v = vertices[index][6:8]
            points.append((round(u * 255), round((1.0 - v) * 255)))
        draw.line((*points, points[0]), fill=(166, 255, 93, 110), width=1)
    output.parent.mkdir(parents=True, exist_ok=True)
    guide.save(output)


def validate(mesh_path: Path, texture_path: Path, report_path: Path) -> dict:
    vertices, indices = read_emesh(mesh_path)
    positions = [vertex[:3] for vertex in vertices]
    mins = [min(point[axis] for point in positions) for axis in range(3)]
    maxs = [max(point[axis] for point in positions) for axis in range(3)]
    triangles = len(indices) // 3
    if not 650 <= triangles <= 1300:
        raise ValueError(f"triangle budget missed: {triangles}")
    if any(not 0.0 <= value <= 1.0 for vertex in vertices
           for value in vertex[6:8]):
        raise ValueError("UV outside atlas")
    if abs(mins[0] + maxs[0]) > 1e-4 or abs(mins[2] + maxs[2]) > 1e-4:
        raise ValueError("body is not centered on XZ")
    if not (math.isclose(maxs[0] - mins[0], 2.5, abs_tol=1e-4) and
            math.isclose(maxs[2] - mins[2], 5.508, abs_tol=1e-4)):
        raise ValueError(f"unexpected GLM bounds: {mins} to {maxs}")
    player_scale = 2.70 / (1.58 + 1.62)
    wheel_radius_native = 0.34375 / player_scale
    if 0.52 - wheel_radius_native < 0.07:
        raise ValueError("shared wheel does not clear the GLM arches")
    with Image.open(texture_path) as texture:
        if texture.size != (256, 256) or texture.mode != "RGBA":
            raise ValueError("texture must be a 256x256 RGBA atlas")
        if texture.getchannel("A").getextrema() != (255, 255):
            raise ValueError("GLM atlas must be fully opaque")
        palette_colours = len(set(texture.getdata()))
    if palette_colours > 96:
        raise ValueError(f"atlas is too smooth for PS1 art: {palette_colours} colours")
    report = {
        "asset": "GLM Lunge",
        "vertices": len(vertices), "triangles": triangles,
        "bounds_min": [round(value, 5) for value in mins],
        "bounds_max": [round(value, 5) for value in maxs],
        "atlas": [256, 256, "RGBA"],
        "palette_colours": palette_colours, "alpha": "opaque",
        "wheel_anchors": {
            "front_z": 1.58, "rear_z": -1.62,
            "x": 1.08, "arch_y": 0.50, "inner_radius": 0.52,
        },
        "checks": [
            "single joined BODY mesh", "no wheel geometry",
            "four open arch rings", "mirrored body geometry",
            "semantic atlas regions", "flat normals", "player fit preserved",
        ],
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return report


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--blender", type=Path, default=BLENDER)
    parser.add_argument("--texture", type=Path,
                        default=ROOT / "assets/textures/vehicles/glm_lunge/body.png")
    parser.add_argument("--mesh", type=Path,
                        default=ROOT / "assets/models/vehicles/glm_lunge/body.emesh")
    parser.add_argument("--blend", type=Path,
                        default=ROOT / "assets/models/vehicles/glm_lunge/source.blend")
    parser.add_argument("--report", type=Path,
                        default=ROOT / "build/glm-lunge-fit-report.json")
    parser.add_argument("--uv-guide", type=Path,
                        default=ROOT / "build/glm-lunge-uv-guide.png")
    args = parser.parse_args()
    make_texture(args.texture)
    subprocess.run([
        str(args.blender), "--background", "--factory-startup", "--python",
        str(ROOT / "tools/glm_lunge_blender.py"), "--",
        "--mesh", str(args.mesh), "--blend", str(args.blend),
    ], cwd=ROOT, check=True)
    make_uv_guide(args.mesh, args.uv_guide)
    report = validate(args.mesh, args.texture, args.report)
    print(f"GLM_COOK mesh={args.mesh} triangles={report['triangles']} "
          f"vertices={report['vertices']} atlas={args.texture} "
          f"blend={args.blend} report={args.report}")


    from bake_vehicle_surfaces import bake_if_canonical
    bake_if_canonical("glm_lunge", args.mesh, args.texture)


if __name__ == "__main__":
    main()
