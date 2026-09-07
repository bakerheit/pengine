#!/usr/bin/env python3
"""Blender-side source builder and static .emesh cooker for Vesper VX-91."""

from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from vesper_vx91_spec import ATLAS_SIZE, REGIONS


class VehicleBuilder:
    def __init__(self) -> None:
        self.objects: list[bpy.types.Object] = []
        self.materials: dict[str, bpy.types.Material] = {}

    def material(self, name: str) -> bpy.types.Material:
        if name not in self.materials:
            material = bpy.data.materials.new(name)
            material.diffuse_color = (0.04, 0.32, 0.31, 1.0)
            material.roughness = 1.0
            self.materials[name] = material
        return self.materials[name]

    def add_mesh(self, name: str, vertices: list[tuple[float, float, float]],
                 faces: list[tuple[int, ...]], material: str) -> bpy.types.Object:
        mesh = bpy.data.meshes.new(name + "Mesh")
        mesh.from_pydata(vertices, [], faces)
        mesh.validate(verbose=False)
        mesh.update()
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(mesh)
        bm.free()
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        obj.data.materials.append(self.material(material))
        for polygon in mesh.polygons:
            polygon.material_index = 0
            polygon.use_smooth = False
        self.objects.append(obj)
        return obj

    def box(self, name: str, lo: tuple[float, float, float],
            hi: tuple[float, float, float], material: str) -> bpy.types.Object:
        x0, y0, z0 = lo
        x1, y1, z1 = hi
        vertices = [
            (x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
            (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1),
        ]
        faces = [
            (0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
            (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7),
        ]
        return self.add_mesh(name, vertices, faces, material)

    def loft(self, name: str,
             sections: list[tuple[float, list[tuple[float, float]]]],
             material: str) -> bpy.types.Object:
        ring_size = len(sections[0][1])
        vertices = [(x, y, z) for y, ring in sections for x, z in ring]
        faces: list[tuple[int, ...]] = []
        for section in range(len(sections) - 1):
            base = section * ring_size
            nxt = (section + 1) * ring_size
            for index in range(ring_size):
                following = (index + 1) % ring_size
                faces.append((base + index, base + following,
                              nxt + following, nxt + index))
        faces.append(tuple(reversed(range(ring_size))))
        last = (len(sections) - 1) * ring_size
        faces.append(tuple(last + index for index in range(ring_size)))
        return self.add_mesh(name, vertices, faces, material)

    def panel(self, name: str, vertices: list[tuple[float, float, float]],
              material: str, desired: tuple[float, float, float]) -> bpy.types.Object:
        normal = (Vector(vertices[1]) - Vector(vertices[0])).cross(
            Vector(vertices[2]) - Vector(vertices[0]))
        if normal.dot(Vector(desired)) < 0.0:
            vertices = [vertices[0], *reversed(vertices[1:])]
        return self.add_mesh(name, vertices, [tuple(range(len(vertices)))], material)

    def arch(self, name: str, side: float, centre_y: float,
             centre_z: float = 0.55, inner: float = 0.54,
             outer: float = 0.60, segments: int = 14) -> bpy.types.Object:
        x_inner = side * 0.80
        x_outer = side * 1.25
        vertices: list[tuple[float, float, float]] = []
        for index in range(segments + 1):
            angle = math.pi * index / segments
            for x, radius in ((x_inner, outer), (x_outer, outer),
                              (x_inner, inner), (x_outer, inner)):
                vertices.append((x, centre_y + math.cos(angle) * radius,
                                 centre_z + math.sin(angle) * radius))
        faces: list[tuple[int, ...]] = []
        for index in range(segments):
            a = index * 4
            b = (index + 1) * 4
            faces.extend([
                (a + 0, b + 0, b + 1, a + 1),
                (a + 1, b + 1, b + 3, a + 3),
                (a + 3, b + 3, b + 2, a + 2),
                (a + 2, b + 2, b + 0, a + 0),
            ])
        faces.extend([(0, 1, 3, 2),
                      (segments * 4 + 2, segments * 4 + 3,
                       segments * 4 + 1, segments * 4 + 0)])
        return self.add_mesh(name, vertices, faces, "BODY_SIDE")

    def cylinder(self, name: str, centre: tuple[float, float, float],
                 radius: float, depth: float, sides: int,
                 material: str) -> bpy.types.Object:
        cx, cy, cz = centre
        vertices = []
        for y in (cy - depth * 0.5, cy + depth * 0.5):
            for index in range(sides):
                angle = 2.0 * math.pi * index / sides
                vertices.append((cx + math.cos(angle) * radius, y,
                                 cz + math.sin(angle) * radius))
        faces = []
        for index in range(sides):
            nxt = (index + 1) % sides
            faces.append((index, nxt, sides + nxt, sides + index))
        faces.append(tuple(reversed(range(sides))))
        faces.append(tuple(sides + index for index in range(sides)))
        return self.add_mesh(name, vertices, faces, material)

    def join(self) -> bpy.types.Object:
        bpy.ops.object.select_all(action="DESELECT")
        for obj in self.objects:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = self.objects[0]
        bpy.ops.object.join()
        body = bpy.context.view_layer.objects.active
        body.name = "BODY"
        body.data.name = "VesperVX91Body"
        return body


def upper_ring(width: float, top: float) -> list[tuple[float, float]]:
    return [(-width * 0.82, 0.74), (-width, 0.84), (-width * 0.92, top),
            (width * 0.92, top), (width, 0.84), (width * 0.82, 0.74)]


def cabin_ring(shoulder: float, roof_width: float,
               roof_z: float) -> list[tuple[float, float]]:
    return [(-shoulder, 0.82), (-shoulder, 1.08),
            (-roof_width, roof_z - 0.12), (-roof_width * 0.82, roof_z),
            (roof_width * 0.82, roof_z), (roof_width, roof_z - 0.12),
            (shoulder, 1.08), (shoulder, 0.82)]


def side_panel(builder: VehicleBuilder, name: str, side: float,
               yz: list[tuple[float, float]], material: str,
               x: float = 1.006) -> None:
    builder.panel(name, [(side * x, y, z) for y, z in yz], material,
                  (side, 0.0, 0.0))


def build_vehicle() -> bpy.types.Object:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    builder = VehicleBuilder()
    builder.box("ChassisCore", (-0.67, -2.92, 0.30), (0.67, 2.92, 0.74),
                "BODY_SHADOW")
    builder.box("Floor", (-0.76, -2.60, 0.22), (0.76, 2.66, 0.34),
                "CLADDING")

    upper_sections = [
        (-3.12, upper_ring(0.88, 0.87)), (-2.65, upper_ring(0.96, 0.99)),
        (-1.22, upper_ring(0.98, 1.08)), (0.38, upper_ring(0.98, 1.18)),
        (1.20, upper_ring(0.96, 1.14)), (2.16, upper_ring(0.91, 1.02)),
        (2.78, upper_ring(0.86, 0.88)), (3.12, upper_ring(0.82, 0.78)),
    ]
    builder.loft("UpperBody", upper_sections, "BODY_TOP")

    cabin_sections = [
        (-2.08, cabin_ring(0.88, 0.82, 1.05)),
        (-1.68, cabin_ring(0.91, 0.77, 1.37)),
        (-1.18, cabin_ring(0.93, 0.72, 1.67)),
        (-0.72, cabin_ring(0.94, 0.76, 1.78)),
        (-0.30, cabin_ring(0.94, 0.72, 1.73)),
        (0.10, cabin_ring(0.93, 0.68, 1.52)),
        (0.40, cabin_ring(0.91, 0.70, 1.26)),
    ]
    builder.loft("FastbackCabin", cabin_sections, "BODY_SIDE")

    for side in (-1.0, 1.0):
        suffix = "L" if side < 0 else "R"
        builder.arch(f"FrontArch{suffix}", side, 2.08)
        builder.arch(f"RearArch{suffix}", side, -1.86)
        x0, x1 = ((-1.12, -0.79) if side < 0 else (0.79, 1.12))
        builder.box(f"Rocker{suffix}", (x0, -1.26, 0.29),
                    (x1, 1.48, 0.50), "CLADDING")
        builder.box(f"FrontCheek{suffix}", (x0, 2.57, 0.36),
                    (x1, 3.12, 0.78), "BODY_SIDE")
        builder.box(f"RearCheek{suffix}", (x0, -3.12, 0.35),
                    (x1, -2.38, 0.80), "BODY_SIDE")

        side_panel(builder, f"DoorGlass{suffix}", side, [
            (0.06, 1.13), (-0.27, 1.64), (-0.69, 1.69), (-0.69, 1.13),
        ], "GLASS", 0.946)
        side_panel(builder, f"QuarterGlass{suffix}", side, [
            (-0.76, 1.13), (-0.77, 1.67), (-1.13, 1.59), (-1.70, 1.13),
        ], "GLASS", 0.946)
        for index, (y0, y1, z0, z1) in enumerate((
            (0.02, 0.04, 0.58, 1.12), (-1.38, -1.36, 0.58, 1.12),
            (-1.36, 0.02, 0.57, 0.59),
        )):
            side_panel(builder, f"DoorSeam{suffix}{index}", side,
                       [(y0, z0), (y1, z0), (y1, z1), (y0, z1)], "SEAM", 1.008)
        side_panel(builder, f"Handle{suffix}", side,
                   [(-0.98, 1.01), (-0.76, 1.01), (-0.76, 1.055),
                    (-0.98, 1.055)], "METAL", 1.010)
        side_panel(builder, f"Marker{suffix}", side,
                   [(1.38, 0.79), (1.57, 0.79), (1.57, 0.88),
                    (1.38, 0.88)], "MARKER_AMBER", 1.115)

    builder.panel("Windshield", [
        (-0.69, 0.405, 1.15), (0.69, 0.405, 1.15),
        (0.62, -0.265, 1.665), (-0.62, -0.265, 1.665),
    ], "GLASS", (0.0, 1.0, 0.4))
    builder.panel("RearGlass", [
        (0.69, -1.145, 1.59), (-0.69, -1.145, 1.59),
        (-0.79, -1.94, 1.10), (0.79, -1.94, 1.10),
    ], "GLASS", (0.0, -1.0, 0.4))
    builder.box("Dashboard", (-0.67, 0.19, 1.04), (0.67, 0.29, 1.13), "DASH")
    builder.box("SeatLeft", (-0.58, -0.62, 0.94), (-0.12, -0.32, 1.29),
                "INTERIOR")
    builder.box("SeatRight", (0.12, -0.62, 0.94), (0.58, -0.32, 1.29),
                "INTERIOR")

    for side in (-1.0, 1.0):
        cx = side * 0.49
        builder.panel(f"PopupSeam{side}", [
            (cx - 0.28, 1.48, 1.105), (cx + 0.28, 1.48, 1.105),
            (cx + 0.25, 2.02, 1.045), (cx - 0.25, 2.02, 1.045),
        ], "SEAM", (0.0, 0.2, 1.0))
        builder.panel(f"PopupLid{side}", [
            (cx - 0.255, 1.50, 1.109), (cx + 0.255, 1.50, 1.109),
            (cx + 0.225, 1.99, 1.050), (cx - 0.225, 1.99, 1.050),
        ], "BODY_TOP", (0.0, 0.2, 1.0))

    builder.box("FrontBumper", (-1.25, 3.10, 0.37), (1.25, 3.22, 0.48),
                "CLADDING")
    builder.panel("FrontGrille", [
        (-0.66, 3.219, 0.49), (0.66, 3.219, 0.49),
        (0.60, 3.219, 0.62), (-0.60, 3.219, 0.62),
    ], "BLACK", (0.0, 1.0, 0.0))
    builder.box("RearBumper", (-1.25, -3.22, 0.38), (1.25, -3.10, 0.49),
                "CLADDING")
    builder.panel("TailCentre", [
        (-0.37, -3.219, 0.58), (0.37, -3.219, 0.58),
        (0.37, -3.219, 0.86), (-0.37, -3.219, 0.86),
    ], "LENS_DARK", (0.0, -1.0, 0.0))
    for side in (-1.0, 1.0):
        x0, x1 = sorted((side * 0.38, side * 0.92))
        builder.panel(f"TailRed{side}", [
            (x0, -3.219, 0.58), (x1, -3.219, 0.58),
            (x1, -3.219, 0.86), (x0, -3.219, 0.86),
        ], "TAIL_RED", (0.0, -1.0, 0.0))
        marker_x0, marker_x1 = sorted((side * 0.93, side * 1.08))
        builder.panel(f"TailAmber{side}", [
            (marker_x0, -3.219, 0.58), (marker_x1, -3.219, 0.58),
            (marker_x1, -3.219, 0.86), (marker_x0, -3.219, 0.86),
        ], "TAIL_AMBER", (0.0, -1.0, 0.0))

    builder.loft("Ducktail", [
        (-2.92, [(-0.90, 1.00), (-0.90, 1.10), (0.90, 1.10), (0.90, 1.00)]),
        (-2.70, [(-0.86, 1.00), (-0.86, 1.19), (0.86, 1.19), (0.86, 1.00)]),
    ], "BODY_TOP")
    builder.cylinder("ExhaustL", (-0.70, -3.11, 0.31), 0.075, 0.22, 8, "EXHAUST")
    builder.cylinder("ExhaustR", (0.70, -3.11, 0.31), 0.075, 0.22, 8, "EXHAUST")

    body = builder.join()
    assign_uvs(body)
    for name, location in {
        "WHEEL_FL": (-1.08, 2.08, 0.55), "WHEEL_FR": (1.08, 2.08, 0.55),
        "WHEEL_RL": (-1.08, -1.86, 0.55), "WHEEL_RR": (1.08, -1.86, 0.55),
    }.items():
        empty = bpy.data.objects.new(name, None)
        empty.empty_display_type = "SPHERE"
        empty.empty_display_size = 0.12
        empty.location = location
        bpy.context.collection.objects.link(empty)
    return body


def assign_uvs(body: bpy.types.Object, regions=REGIONS) -> None:
    mesh = body.data
    uv_layer = mesh.uv_layers.new(name="UVMap")
    for polygon in mesh.polygons:
        material_name = body.material_slots[polygon.material_index].material.name
        x0, y0, x1, y1 = regions[material_name]
        u0, u1 = (x0 + 2) / ATLAS_SIZE, (x1 - 2) / ATLAS_SIZE
        v0, v1 = 1.0 - (y1 - 2) / ATLAS_SIZE, 1.0 - (y0 + 2) / ATLAS_SIZE
        normal = polygon.normal
        if abs(normal.z) >= abs(normal.x) and abs(normal.z) >= abs(normal.y):
            axes = (0, 1)
        elif abs(normal.y) >= abs(normal.x):
            axes = (0, 2)
        else:
            axes = (1, 2)
        # Use stable model-space extents, not each polygon's own bounds. This
        # keeps highlights continuous and prevents a stripe from repeating on
        # every triangle after triangulation.
        axis_bounds = ((-1.25, 1.25), (-3.22, 3.22), (0.22, 1.78))
        a_min, a_max = axis_bounds[axes[0]]
        b_min, b_max = axis_bounds[axes[1]]
        coords = [mesh.vertices[mesh.loops[index].vertex_index].co
                  for index in polygon.loop_indices]
        for loop_index, co in zip(polygon.loop_indices, coords):
            a = max(0.0, min(1.0, (co[axes[0]] - a_min) / (a_max - a_min)))
            b = max(0.0, min(1.0, (co[axes[1]] - b_min) / (b_max - b_min)))
            uv_layer.data[loop_index].uv = (u0 + a * (u1 - u0),
                                            v0 + b * (v1 - v0))


def tangent_for(p0: Vector, p1: Vector, p2: Vector,
                uv0: Vector, uv1: Vector, uv2: Vector, normal: Vector) -> Vector:
    edge1, edge2 = p1 - p0, p2 - p0
    duv1, duv2 = uv1 - uv0, uv2 - uv0
    denominator = duv1.x * duv2.y - duv1.y * duv2.x
    if abs(denominator) > 1e-8:
        tangent = (edge1 * duv2.y - edge2 * duv1.y) / denominator
        if tangent.length_squared > 1e-10:
            return tangent.normalized()
    reference = Vector((0.0, 1.0, 0.0))
    if abs(normal.dot(reference)) > 0.9:
        reference = Vector((1.0, 0.0, 0.0))
    return normal.cross(reference).normalized()


def export_emesh(body: bpy.types.Object, output: Path,
                 material_name: str = "vesper_vx91") -> tuple[int, int]:
    mesh = body.data
    mesh.calc_loop_triangles()
    uv_layer = mesh.uv_layers.active.data
    vertices: list[tuple[float, ...]] = []
    indices: list[int] = []
    for triangle in mesh.loop_triangles:
        loop_indices = [triangle.loops[0], triangle.loops[2], triangle.loops[1]]
        points, normals, uvs = [], [], []
        for loop_index in loop_indices:
            loop = mesh.loops[loop_index]
            co = mesh.vertices[loop.vertex_index].co
            normal = loop.normal.normalized()
            points.append(Vector((co.x, co.z, co.y)))
            normals.append(Vector((normal.x, normal.z, normal.y)))
            uv = uv_layer[loop_index].uv
            uvs.append(Vector((uv.x, uv.y)))
        tangent = tangent_for(points[0], points[1], points[2],
                              uvs[0], uvs[1], uvs[2], normals[0])
        start = len(vertices)
        for point, normal, uv in zip(points, normals, uvs):
            vertices.append((point.x, point.y, point.z,
                             normal.x, normal.y, normal.z,
                             uv.x, uv.y, tangent.x, tangent.y, tangent.z, 1.0))
        indices.extend((start, start + 1, start + 2))
    material = material_name.encode("utf-8") + b"\0"
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as cooked:
        cooked.write(struct.pack("<8I", 0x48534D45, 2, 0, len(vertices),
                                 len(indices), 1, len(material), 0))
        for vertex in vertices:
            cooked.write(struct.pack("<12f", *vertex))
        cooked.write(struct.pack(f"<{len(indices)}I", *indices))
        cooked.write(struct.pack("<4I", 0, len(indices), 0, 0))
        cooked.write(material)
    return len(vertices), len(indices) // 3


def main() -> None:
    args_source = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesh", type=Path, required=True)
    parser.add_argument("--blend", type=Path, required=True)
    args = parser.parse_args(args_source)
    body = build_vehicle()
    args.blend.parent.mkdir(parents=True, exist_ok=True)
    bpy.context.view_layer.objects.active = body
    body.select_set(True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    vertices, triangles = export_emesh(body, args.mesh)
    print(f"VESPER_BLENDER body=BODY vertices={vertices} triangles={triangles} "
          f"blend={args.blend} mesh={args.mesh}")


if __name__ == "__main__":
    main()
