#!/usr/bin/env python3
"""Add 45 and 90 degree curves to the purchased Blender road pack.

Run with Blender, for example:

    blender -b -P tools/extend_psx_road_pack.py -- SOURCE.blend OUTPUT.blend

The source pack stays untouched. The curves use its packed RoadTexture1
material and 6 m road width. A separate GLB of the four new pieces is saved
beside the output blend for easy inspection/import.
"""

import math
from pathlib import Path
import sys

import bpy


def make_curve(name: str, degrees: int, turn: int, material, collection):
    radius = 12.0  # 90 degree ends land 12 m east/north, on the pack's 6 m grid
    width = 6.0
    segments = degrees // 5
    lateral_steps = 2
    angle = math.radians(degrees)
    vertices = []
    for level in (0.05, 0.0):
        for i in range(segments + 1):
            theta = angle * i / segments
            cx = radius * math.sin(theta)
            cy = turn * radius * (1.0 - math.cos(theta))
            nx = -turn * math.sin(theta)
            ny = math.cos(theta)
            for j in range(lateral_steps + 1):
                across = width * (j / lateral_steps - 0.5)
                vertices.append((cx + nx * across, cy + ny * across, level))

    row = lateral_steps + 1
    bottom = (segments + 1) * row
    faces = []
    uv_faces = []

    def add_face(indices, uvs):
        faces.append(indices)
        uv_faces.append(uvs)

    for i in range(segments):
        v0 = radius * angle * i / segments / 6.0
        v1 = radius * angle * (i + 1) / segments / 6.0
        for j in range(lateral_steps):
            u0 = j / lateral_steps
            u1 = (j + 1) / lateral_steps
            a = i * row + j
            b = (i + 1) * row + j
            c = (i + 1) * row + j + 1
            d = i * row + j + 1
            add_face((a, b, c, d), ((u0, v0), (u0, v1), (u1, v1), (u1, v0)))
            add_face((bottom + d, bottom + c, bottom + b, bottom + a),
                     ((u1, v0), (u1, v1), (u0, v1), (u0, v0)))

        # Both kerb faces close the original pack's 5 cm thick road slab.
        for j in (0, lateral_steps):
            a = i * row + j
            b = (i + 1) * row + j
            indices = (a, bottom + a, bottom + b, b)
            uvs = ((0.0, v0), (0.0, v0 + 0.05 / 6.0),
                   (0.0, v1 + 0.05 / 6.0), (0.0, v1))
            if j == lateral_steps:
                indices = tuple(reversed(indices))
                uvs = tuple(reversed(uvs))
            add_face(indices, uvs)

    for i in (0, segments):
        for j in range(lateral_steps):
            a = i * row + j
            b = i * row + j + 1
            indices = (a, b, bottom + b, bottom + a)
            uvs = ((j / lateral_steps, 0.0), ((j + 1) / lateral_steps, 0.0),
                   ((j + 1) / lateral_steps, 0.05 / 6.0),
                   (j / lateral_steps, 0.05 / 6.0))
            if i == 0:
                indices = tuple(reversed(indices))
                uvs = tuple(reversed(uvs))
            add_face(indices, uvs)

    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.materials.append(material)
    uv_layer = mesh.uv_layers.new(name="UVMap")
    for polygon, face_uvs in zip(mesh.polygons, uv_faces):
        for loop_index, uv in zip(polygon.loop_indices, face_uvs):
            uv_layer.data[loop_index].uv = uv
    mesh.update()

    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    obj["road_width_m"] = width
    obj["centreline_radius_m"] = radius
    obj["entry_center_xy"] = "0, 0"
    obj["exit_center_xy"] = f"{radius * math.sin(angle):.3f}, {turn * radius * (1.0 - math.cos(angle)):.3f}"
    obj["entry_heading_deg"] = 0
    obj["exit_heading_deg"] = turn * degrees
    return obj


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(args) != 2:
        raise SystemExit("usage: blender -b -P extend_psx_road_pack.py -- SOURCE.blend OUTPUT.blend")
    source, output = map(Path, args)
    if not source.is_file():
        raise SystemExit(f"source blend not found: {source}")
    if output.exists():
        raise SystemExit(f"output already exists: {output}")
    if output.suffix.lower() != ".blend":
        raise SystemExit("output must be a .blend file")

    bpy.ops.wm.open_mainfile(filepath=str(source))
    material = bpy.data.materials.get("RoadTexture1")
    if material is None:
        raise SystemExit("RoadTexture1 material is missing from the source pack")
    collection = bpy.data.collections.new("Curved Roads - Probable Cause")
    bpy.context.scene.collection.children.link(collection)
    created = [
        make_curve(f"Road Curve {degrees} {side} R12", degrees, turn,
                   material, collection)
        for degrees in (45, 90)
        for side, turn in (("Left", 1), ("Right", -1))
    ]

    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output))

    bpy.ops.object.select_all(action="DESELECT")
    for obj in created:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = created[0]
    glb = output.with_suffix(".glb")
    bpy.ops.export_scene.gltf(filepath=str(glb), export_format="GLB",
                              use_selection=True)
    print(f"CURVE_PACK_BLEND={output}")
    print(f"CURVE_PACK_GLB={glb}")


if __name__ == "__main__":
    main()
