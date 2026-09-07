#!/usr/bin/env python3
"""Model-first cook, imagegen texture reduction and QA for Fang Venom v2."""

import argparse
import hashlib
import json
import math
import struct
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from fang_venom_v2_spec import ATLAS_SIZE, BASE, REGIONS, SHAPE, WHEELS
from make_vesper_vx91_assets import read_emesh
from render_firetruck_preview import Part, read_part, raster_view


ROOT = Path(__file__).resolve().parents[1]
BLENDER = Path("/Applications/Blender.app/Contents/MacOS/Blender")
MODEL = ROOT / "assets/models/vehicles/fang_venom_v2"
TEXTURE_DIR = ROOT / "assets/textures/vehicles/fang_venom_v2"
TEXTURE = TEXTURE_DIR / "body.png"
IMAGEGEN_SOURCE = TEXTURE_DIR / "body-imagegen-source.png"
TEMPLATE = ROOT / "build/fang-venom-v2-component-map.png"
UV_GUIDE = ROOT / "build/fang-venom-v2-uv-guide.png"
MODEL_PREVIEW = ROOT / "build/fang-venom-v2-model-first.png"
PREVIEW = ROOT / "build/fang-venom-v2-preview.png"
REPORT = ROOT / "build/fang-venom-v2-fit-report.json"
MEASUREMENTS = ROOT / "build/fang-venom-v2-measurements.json"


def content_box(box):
    x0, y0, x1, y1 = box
    return x0 + 3, y0 + 12, x1 - 3, y1 - 3


def make_component_map():
    image = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (7, 9, 12, 255))
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()
    labels = {
        "PAINT_SIDE": "SIDE", "PAINT_TOP": "TOP", "PAINT_LOWER": "LOWER",
        "HEADLIGHT": "HEAD", "TAIL_RED": "TAIL", "EXHAUST": "EXH",
    }
    for name, box in REGIONS.items():
        x0, y0, x1, y1 = box
        draw.rectangle(box, fill=BASE[name], outline=(232, 153, 62, 255), width=1)
        draw.rectangle((x0 + 1, y0 + 1, x1 - 1, min(y1 - 1, y0 + 10)),
                       fill=(9, 11, 15, 255))
        max_chars = max(2, (x1 - x0 - 4) // 6)
        draw.text((x0 + 2, y0 + 2), labels.get(name, name)[:max_chars], font=font,
                  fill=(241, 224, 184, 255))
        cx0, cy0, cx1, cy1 = content_box(box)
        # Measured direction hints help imagegen make material texture that
        # follows the UV islands rather than painting a pictures vehicle.
        if name.startswith("PAINT"):
            draw.line((cx0, cy1 - 4, cx1, cy0 + 5), fill=(83, 31, 116, 255), width=3)
            draw.line((cx0, cy1 - 1, cx1, cy0 + 8), fill=(210, 194, 151, 255), width=1)
        elif name in {"METAL", "RIM", "BRAKE", "EXHAUST"}:
            for y in range(cy0 + 3, cy1, 6):
                draw.line((cx0, y, cx1, y), fill=(190, 193, 187, 255), width=1)
        elif name in {"ENGINE", "FRAME", "CHAIN"}:
            for x in range(cx0 + 3, cx1, 7):
                draw.line((x, cy0, x, cy1), fill=(92, 91, 91, 255), width=1)
    TEMPLATE.parent.mkdir(parents=True, exist_ok=True)
    image.save(TEMPLATE)
    return image


def write_model_first_texture():
    TEXTURE_DIR.mkdir(parents=True, exist_ok=True)
    make_component_map().save(TEXTURE)


def build_model():
    if not BLENDER.is_file():
        raise FileNotFoundError(f"Blender missing: {BLENDER}")
    subprocess.run([
        str(BLENDER), "--background", "--factory-startup", "--python",
        str(ROOT / "tools/fang_venom_v2_blender.py"), "--",
        "--model-dir", str(MODEL), "--texture", str(TEXTURE),
    ], check=True)


def imagegen_texture():
    if not IMAGEGEN_SOURCE.is_file():
        raise FileNotFoundError(f"built-in imagegen edit missing: {IMAGEGEN_SOURCE}")
    source = Image.open(IMAGEGEN_SOURCE).convert("RGB").resize(
        (ATLAS_SIZE, ATLAS_SIZE), Image.Resampling.LANCZOS)
    # Force deliberate four-pixel clusters, then derive a small shared palette
    # from imagegen's material decisions.  This keeps its wear, value breakup
    # and colour ideas while making the result behave like a 1990s game atlas.
    source = source.resize((64, 64), Image.Resampling.BOX).resize(
        (ATLAS_SIZE, ATLAS_SIZE), Image.Resampling.NEAREST)
    source = source.quantize(colors=72, method=Image.Quantize.MEDIANCUT).convert("RGB")
    final = Image.new("RGBA", (ATLAS_SIZE, ATLAS_SIZE), (7, 9, 12, 255))
    draw = ImageDraw.Draw(final)
    for name, box in REGIONS.items():
        crop = source.crop(box).convert("RGBA")
        tint = Image.new("RGBA", crop.size, BASE[name])
        strength = .12 if name.startswith("PAINT") or name == "DECAL" else .24
        crop = Image.blend(crop, tint, strength)
        final.paste(crop, box)
        x0, y0, x1, _ = box
        draw.rectangle((x0, y0, x1, y0 + 11), fill=BASE[name])

    # Lock safety-critical lens identity without replacing the generated inner
    # texture.  UVs hit these complete cells, so dark frames remain readable.
    lens_specs = {
        "HEADLIGHT": ((77, 75, 67, 255), (246, 238, 198, 255)),
        "TAIL_RED": ((72, 6, 14, 255), (235, 36, 45, 255)),
        "AMBER": ((91, 32, 4, 255), (247, 129, 18, 255)),
        "GAUGE": ((8, 20, 24, 255), (54, 135, 143, 255)),
    }
    for name, (frame, highlight) in lens_specs.items():
        x0, y0, x1, y1 = content_box(REGIONS[name])
        draw.rectangle((x0, y0, x1, y1), outline=frame, width=2)
        draw.line((x0 + 3, y0 + 3, x1 - 3, y0 + 3), fill=highlight, width=2)
    final.save(TEXTURE, optimize=True)


def mesh_data(name):
    vertices, indices = read_emesh(MODEL / name)
    return np.asarray(vertices, dtype=np.float64), np.asarray(indices, dtype=np.int64)


def assembled_parts():
    body = read_part(MODEL / "body.emesh", TEXTURE)
    front = read_part(MODEL / "front_wheel.emesh", TEXTURE)
    rear = read_part(MODEL / "rear_wheel.emesh", TEXTURE)
    glass = read_part(MODEL / "windshield.emesh", TEXTURE)
    return [
        body,
        Part(front.positions + np.array((0, WHEELS["centre_y"], WHEELS["front_z"])),
             front.normals, front.uvs, front.indices, front.texture),
        Part(rear.positions + np.array((0, WHEELS["centre_y"], WHEELS["rear_z"])),
             rear.normals, rear.uvs, rear.indices, rear.texture),
        glass,
    ]


def preview(output):
    parts = assembled_parts()
    views = [("FRONT 3/4", -32, 18), ("SIDE", -90, 3),
             ("REAR 3/4", -148, 17), ("ELEVATED", -35, 33),
             ("FRONT", 0, 4), ("REAR", 180, 4)]
    sheet = Image.new("RGB", (1440, 840), (18, 21, 24))
    draw = ImageDraw.Draw(sheet)
    for index, (label, yaw, pitch) in enumerate(views):
        view = raster_view(parts, yaw, pitch, 480, 386)
        x, y = (index % 3) * 480, (index // 3) * 420
        sheet.paste(view, (x, y))
        draw.text((x + 12, y + 397), label, fill=(232, 228, 210))
    sheet.save(output)


def uv_guide():
    image = Image.open(TEXTURE).copy()
    draw = ImageDraw.Draw(image)
    for filename in ("body.emesh", "front_wheel.emesh", "rear_wheel.emesh",
                     "windshield.emesh"):
        vertices, indices = read_emesh(MODEL / filename)
        data = np.asarray(vertices)
        for triangle in np.asarray(indices).reshape(-1, 3):
            points = [(data[i, 6] * 256, (1 - data[i, 7]) * 256) for i in triangle]
            draw.line(points + [points[0]], fill=(244, 157, 55, 170), width=1)
    image.save(UV_GUIDE)


def write_qa_fixture():
    values = []
    for part in assembled_parts():
        for triangle in part.indices:
            for index in triangle:
                values.append((*part.positions[index], *part.normals[index],
                               *part.uvs[index], 1, 0, 0, 1))
    vertices = np.asarray(values, dtype="<f4")
    path = ROOT / "build/fang-venom-v2-qa.emesh"
    material = b"fang_venom_v2\0"
    with path.open("wb") as output:
        output.write(struct.pack("<8I", 0x48534D45, 2, 0, len(vertices),
                                 len(vertices), 1, len(material), 0))
        output.write(vertices.tobytes())
        output.write(np.arange(len(vertices), dtype="<u4").tobytes())
        output.write(struct.pack("<4I", 0, len(vertices), 0, 0))
        output.write(material)
    return path


def validate(textured):
    body, body_indices = mesh_data("body.emesh")
    front, front_indices = mesh_data("front_wheel.emesh")
    rear, rear_indices = mesh_data("rear_wheel.emesh")
    glass, glass_indices = mesh_data("windshield.emesh")
    datasets = ((body, body_indices), (front, front_indices),
                (rear, rear_indices), (glass, glass_indices))
    for vertices, indices in datasets:
        assert len(indices) and len(indices) % 3 == 0
        assert np.isfinite(vertices).all()
        assert indices.min() >= 0 and indices.max() < len(vertices)
        assert ((vertices[:, 6:8] >= 0) & (vertices[:, 6:8] <= 1)).all()

    body_triangles = len(body_indices) // 3
    front_triangles = len(front_indices) // 3
    rear_triangles = len(rear_indices) // 3
    assert SHAPE["body_triangle_budget"][0] <= body_triangles <= SHAPE["body_triangle_budget"][1]
    for count in (front_triangles, rear_triangles):
        assert SHAPE["wheel_triangle_budget"][0] <= count <= SHAPE["wheel_triangle_budget"][1]
    body_size = np.ptp(body[:, :3], axis=0)
    assert .80 <= body_size[0] <= .83, body_size
    assert .90 <= body_size[1] <= .97, body_size
    assert 2.23 <= body_size[2] <= 2.27, body_size
    front_radius = max(np.ptp(front[:, 1]), np.ptp(front[:, 2])) * .5
    rear_radius = max(np.ptp(rear[:, 1]), np.ptp(rear[:, 2])) * .5
    assert abs(front_radius - WHEELS["radius"]) < .003
    assert abs(rear_radius - WHEELS["radius"]) < .003
    assert np.ptp(front[:, 0]) < np.ptp(rear[:, 0])
    assert hashlib.sha256((MODEL / "front_wheel.emesh").read_bytes()).digest() != \
        hashlib.sha256((MODEL / "rear_wheel.emesh").read_bytes()).digest()

    image = Image.open(TEXTURE)
    assert image.size == (256, 256) and image.mode == "RGBA"
    assert image.getchannel("A").getextrema() == (255, 255)
    if textured:
        assert IMAGEGEN_SOURCE.is_file()
    report = {
        "model": "Fang Venom",
        "attempt": 2,
        "phase": "imagegen-textured" if textured else "model-first",
        "shape_contract": SHAPE,
        "body_triangles": body_triangles,
        "front_wheel_triangles": front_triangles,
        "rear_wheel_triangles": rear_triangles,
        "glass_triangles": len(glass_indices) // 3,
        "body_dimensions_m": [round(float(value), 5) for value in body_size],
        "wheel_radius_m": round(float(front_radius), 5),
        "wheelbase_m": WHEELS["front_z"] - WHEELS["rear_z"],
        "front_tire_width_m": round(float(np.ptp(front[:, 0])), 5),
        "rear_tire_width_m": round(float(np.ptp(rear[:, 0])), 5),
        "atlas": [256, 256, "RGBA"],
        "imagegen": {
            "mode": "built-in imagegen edit" if textured else "not run yet",
            "source": str(IMAGEGEN_SOURCE.relative_to(ROOT)) if textured else None,
            "target": str(TEXTURE.relative_to(ROOT)),
        },
        "mesh_sha256": hashlib.sha256((MODEL / "body.emesh").read_bytes()).hexdigest(),
        "texture_sha256": hashlib.sha256(TEXTURE.read_bytes()).hexdigest(),
        "checks": [
            "model completed before image generation",
            "body and glass are wheel-free",
            "distinct front brake and rear chain-drive wheel meshes",
            "finite indexed triangles and inset semantic UVs",
            "measured body bounds, tire widths, radius and wheelbase",
            "opaque 256px imagegen-derived PSX atlas" if textured else
            "labeled 256px component map ready for imagegen",
        ],
    }
    REPORT.write_text(json.dumps(report, indent=2) + "\n")
    MEASUREMENTS.write_text(json.dumps({
        "shape_contract": SHAPE,
        "measured": {
            "body_dimensions_m": report["body_dimensions_m"],
            "wheel_radius_m": report["wheel_radius_m"],
            "wheelbase_m": report["wheelbase_m"],
            "front_tire_width_m": report["front_tire_width_m"],
            "rear_tire_width_m": report["rear_tire_width_m"],
        },
        "atlas_regions_px": REGIONS,
    }, indent=2) + "\n")
    uv_guide()
    preview(PREVIEW if textured else MODEL_PREVIEW)
    write_qa_fixture()
    print(json.dumps(report, indent=2))


def asset_lab():
    mesh = write_qa_fixture()
    screenshot = ROOT / "build/fang-venom-v2-asset-lab.png"
    process = subprocess.run([
        str(ROOT / "build/bin/apricot_asset_lab"), "--model", str(mesh),
        "--texture", str(TEXTURE), "--yaw", "212", "--frames", "90",
        "--screenshot", str(screenshot),
    ], cwd=ROOT, capture_output=True, text=True)
    (ROOT / "build/fang-venom-v2-asset-lab.log").write_text(
        process.stdout + process.stderr)
    process.check_returncode()
    assert "0 GL errors" in process.stdout
    print("Fang Venom v2 Asset Lab: 90 frames, 0 GL errors")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--prepare-imagegen", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--asset-lab", action="store_true")
    args = parser.parse_args()
    MODEL.mkdir(parents=True, exist_ok=True)
    TEXTURE_DIR.mkdir(parents=True, exist_ok=True)
    if args.prepare_imagegen:
        write_model_first_texture()
    elif not args.validate_only:
        imagegen_texture()
    if not args.validate_only:
        build_model()
    validate(textured=not args.prepare_imagegen)
    if args.asset_lab:
        asset_lab()


if __name__ == "__main__":
    main()
