#!/usr/bin/env python3
"""Model-first Municipal ambulance built with the Harrow Workman method."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from municipal_ambulance_spec import ATLAS_SIZE, REGIONS, WHEELS
from vesper_vx91_blender import VehicleBuilder, export_emesh


class AmbulanceBuilder(VehicleBuilder):
    def panel(self, name, vertices, material, desired):
        obj = super().panel(name, vertices, material, desired)
        # Open panels have no volume from which Blender can infer outside.
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        for face in bm.faces:
            if face.normal.dot(Vector(desired)) < 0:
                face.normal_flip()
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()
        return obj


def shell_panel(builder, name, points, thickness, material="MODULE_WHITE"):
    """Create a capped thin wall around an opening instead of a filled box."""
    count = len(points)
    offset = Vector(thickness)
    vertices = points + [tuple(Vector(point) + offset) for point in points]
    faces = [tuple(range(count)), tuple(range(2 * count - 1, count - 1, -1))]
    faces += [
        (index, (index + 1) % count, (index + 1) % count + count, index + count)
        for index in range(count)
    ]
    return builder.add_mesh(name, vertices, faces, material)


def window_frame(builder, name, outer, inner, thickness, material="CAB_BODY"):
    """Capped four-sided rim surrounding a real transparent opening."""
    points = outer + inner
    points += [tuple(Vector(point) + Vector(thickness)) for point in points]
    faces = []
    for index in range(4):
        following = (index + 1) % 4
        faces += [
            (index, following, following + 4, index + 4),
            (index + 8, index + 12, following + 12, following + 8),
            (index, index + 8, following + 8, following),
            (index + 4, following + 4, following + 12, index + 12),
        ]
    return builder.add_mesh(name, points, faces, material)


def side_panel(builder, name, side, yz, material, x=1.084):
    return builder.panel(
        name, [(side * x, y, z) for y, z in yz], material, (side, 0, 0)
    )


def cab_side_x(height):
    return 0.98 - max(0.0, height - 1.24) * (0.10 / 0.65)


def fender(builder, side, axle, front):
    """Workman-style wall whose lower edge traces a true wheel opening."""
    radius = 0.62
    samples = [(-radius, 0.28), (-radius, WHEELS["arch_y"])]
    samples += [
        (-radius * math.cos(index * math.pi / 8),
         WHEELS["arch_y"] + 0.58 * math.sin(index * math.pi / 8))
        for index in range(1, 8)
    ]
    samples += [(radius, WHEELS["arch_y"]), (radius, 0.28)]
    ends = (1.05, 3.00) if front else (-3.00, 0.18)
    samples = [(ends[0] - axle, 0.28)] + samples + [(ends[1] - axle, 0.28)]
    sections = []
    for longitudinal, bottom in samples:
        position = axle + longitudinal
        if front:
            top = 1.22 - max(0.0, position - 1.10) * 0.075
            ring = (
                (0.83, bottom), (1.00, bottom), (1.03, bottom + 0.045),
                (1.03, top - 0.06), (0.96, top), (0.83, top),
            )
        else:
            ring = (
                (0.82, bottom), (1.05, bottom), (1.08, bottom + 0.045),
                (1.08, 1.17), (0.91, 1.17), (0.82, 1.10),
            )
        sections.append((position, [(side * x, height) for x, height in ring]))
    obj = builder.loft(
        ("Front" if front else "Rear") + "Fender" + str(side),
        sections,
        "CAB_BODY" if front else "MODULE_WHITE",
    )
    obj.data.materials.append(builder.material("ROOF"))
    for face in obj.data.polygons:
        if all(obj.data.vertices[index].co.z >= (1.155 if not front else 1.14)
               for index in face.vertices):
            face.material_index = 1


def local_uv_bounds(obj, axes):
    coords = [vertex.co for vertex in obj.data.vertices]
    return tuple(
        (min(point[axis] for point in coords), max(point[axis] for point in coords))
        for axis in axes
    )


def map_object(obj):
    mesh = obj.data
    uv = mesh.uv_layers.new(name="UVMap")
    detail_materials = {
        "GLASS", "REAR_DOOR", "SIDE_DOOR", "MEDICAL", "EMS_TEXT",
        "GRILLE", "METAL", "HEADLIGHT", "AMBER", "TAIL_RED", "REVERSE",
        "LIGHT_RED", "LIGHT_BLUE", "BLACK", "CLADDING", "INTERIOR", "SEAM",
    }
    global_bounds = ((-1.15, 1.15), (-3.05, 3.05), (0.22, 2.62))
    for polygon in mesh.polygons:
        material_name = obj.data.materials[polygon.material_index].name
        x0, y0, x1, y1 = REGIONS[material_name]
        normal = polygon.normal
        if abs(normal.z) >= max(abs(normal.x), abs(normal.y)):
            axes = (0, 1)
        elif abs(normal.y) > abs(normal.x):
            axes = (0, 2)
        else:
            axes = (1, 2)
        points = [mesh.vertices[mesh.loops[index].vertex_index].co
                  for index in polygon.loop_indices]
        bounds = local_uv_bounds(obj, axes) if material_name in detail_materials else (
            global_bounds[axes[0]], global_bounds[axes[1]]
        )
        for loop_index, point in zip(polygon.loop_indices, points):
            mapped = [
                (point[axis] - low) / (high - low) if high - low > 1e-6 else 0.5
                for axis, (low, high) in zip(axes, bounds)
            ]
            uv.data[loop_index].uv = (
                (x0 + 2 + mapped[0] * (x1 - x0 - 4)) / ATLAS_SIZE,
                1 - (y1 - 2 - mapped[1] * (y1 - y0 - 4)) / ATLAS_SIZE,
            )


def build_cab(builder):
    # Lower cab, then a genuinely hollow upper shell with framed glass.
    builder.box("CabLower", (-0.98, 0.20, 0.28), (0.98, 1.36, 1.24), "CAB_BODY")
    builder.loft("CabRoof", [
        (0.20, [(-0.88, 1.86), (-0.79, 1.98), (0.79, 1.98), (0.88, 1.86)]),
        (0.91, [(-0.88, 1.86), (-0.79, 1.98), (0.79, 1.98), (0.88, 1.86)]),
    ], "ROOF")

    windshield_outer = [
        (-0.98, 1.36, 1.24), (0.98, 1.36, 1.24),
        (0.88, 0.91, 1.88), (-0.88, 0.91, 1.88),
    ]
    windshield_inner = [
        (-0.84, 1.31, 1.32), (0.84, 1.31, 1.32),
        (0.76, 0.95, 1.81), (-0.76, 0.95, 1.81),
    ]
    window_frame(builder, "WindshieldFrame", windshield_outer,
                 windshield_inner, (0, -0.035, 0))
    builder.panel("WindshieldGlass", [
        (-0.835, 1.305, 1.325), (0.835, 1.305, 1.325),
        (0.755, 0.945, 1.805), (-0.755, 0.945, 1.805),
    ], "GLASS", (0, 1, 0.5))

    for side in (-1, 1):
        outer_yz = [(0.24, 1.24), (1.34, 1.24), (0.91, 1.88), (0.24, 1.88)]
        inner_yz = [(0.32, 1.32), (1.23, 1.32), (0.88, 1.80), (0.32, 1.80)]
        outer = [(side * cab_side_x(height), y, height) for y, height in outer_yz]
        inner = [(side * cab_side_x(height), y, height) for y, height in inner_yz]
        window_frame(builder, "SideWindowFrame" + str(side), outer, inner,
                     (-side * 0.045, 0, 0))
        builder.panel("SideGlass" + str(side), [
            (side * (cab_side_x(height) + 0.004), y, height)
            for y, height in inner_yz
        ], "GLASS", (side, 0, 0))
        side_panel(builder, "CabDoorPaint" + str(side), side,
                   [(0.25, 0.48), (1.32, 0.48), (1.32, 1.20), (0.25, 1.20)],
                   "CAB_BODY", 0.984)
        side_panel(builder, "CabStripe" + str(side), side,
                   [(0.25, 1.02), (1.32, 1.02), (1.32, 1.17), (0.25, 1.17)],
                   "STRIPE", 0.989)
        side_panel(builder, "CabDoorSeam" + str(side), side,
                   [(0.25, 0.49), (0.27, 0.49), (0.27, 1.20), (0.25, 1.20)],
                   "SEAM", 0.992)
        a, c = sorted((side * 0.96, side * 1.13))
        builder.box("MirrorArm" + str(side), (a, 1.05, 1.36),
                    (c, 1.11, 1.42), "METAL")
        a, c = sorted((side * 1.10, side * 1.18))
        builder.box("Mirror" + str(side), (a, 0.94, 1.34),
                    (c, 1.16, 1.55), "CLADDING")
        side_panel(builder, "MirrorGlass" + str(side), side,
                   [(0.96, 1.37), (1.14, 1.37), (1.14, 1.52), (0.96, 1.52)],
                   "METAL", 1.184)

    builder.box("Dashboard", (-0.82, 1.03, 1.08), (0.82, 1.25, 1.25), "INTERIOR")
    for side in (-1, 1):
        x0, x1 = sorted((side * 0.16, side * 0.68))
        builder.box("Seat" + str(side), (x0, 0.39, 0.58),
                    (x1, 0.80, 1.32), "INTERIOR")


def build_module(builder):
    # Narrow floor and chassis stay between the tyres. Side skins make the arches.
    builder.box("ModuleFloor", (-0.80, -2.95, 0.28), (0.80, 0.18, 0.44), "SHADOW")
    builder.box("ModuleRoof", (-1.08, -3.00, 2.44), (1.08, 0.20, 2.56), "ROOF")
    builder.box("ModuleFront", (-1.08, 0.12, 1.17), (1.08, 0.20, 2.44), "MODULE_WHITE")

    # Driver side is a continuous service wall. The passenger side is split
    # around a real side-door/window opening.
    shell_panel(builder, "DriverModuleWall", [
        (1.08, -3.00, 1.17), (1.08, 0.20, 1.17),
        (1.08, 0.20, 2.44), (1.08, -3.00, 2.44),
    ], (-0.06, 0, 0))
    shell_panel(builder, "PassengerRearWall", [
        (-1.08, -3.00, 1.17), (-1.08, -0.66, 1.17),
        (-1.08, -0.66, 2.44), (-1.08, -3.00, 2.44),
    ], (0.06, 0, 0))
    shell_panel(builder, "PassengerDoorFrontPillar", [
        (-1.08, 0.12, 1.17), (-1.08, 0.20, 1.17),
        (-1.08, 0.20, 2.44), (-1.08, 0.12, 2.44),
    ], (0.06, 0, 0))
    shell_panel(builder, "PassengerDoorHeader", [
        (-1.08, -0.66, 2.28), (-1.08, 0.12, 2.28),
        (-1.08, 0.12, 2.44), (-1.08, -0.66, 2.44),
    ], (0.06, 0, 0))

    # Side door: solid lower half, then a capped frame around transparent glass.
    shell_panel(builder, "SideDoorLower", [
        (-1.084, -0.64, 1.17), (-1.084, 0.10, 1.17),
        (-1.084, 0.10, 1.48), (-1.084, -0.64, 1.48),
    ], (0.045, 0, 0), "SIDE_DOOR")
    window_frame(builder, "SideDoorWindowFrame", [
        (-1.084, -0.64, 1.48), (-1.084, 0.10, 1.48),
        (-1.084, 0.10, 2.28), (-1.084, -0.64, 2.28),
    ], [
        (-1.088, -0.55, 1.58), (-1.088, 0.01, 1.58),
        (-1.088, 0.01, 2.17), (-1.088, -0.55, 2.17),
    ], (0.045, 0, 0), "SIDE_DOOR")
    builder.panel("SideDoorGlass", [
        (-1.090, -0.55, 1.58), (-1.090, 0.01, 1.58),
        (-1.090, 0.01, 2.17), (-1.090, -0.55, 2.17),
    ], "GLASS", (-1, 0, 0))

    for side in (-1, 1):
        x = side * 1.086
        # Livery follows the wall and door instead of being baked across UV seams.
        side_panel(builder, "ModuleStripe" + str(side), side,
                   [(-2.92, 1.23), (0.13, 1.23), (0.13, 1.47), (-2.92, 1.47)],
                   "STRIPE", abs(x))
        # Corner warning lamps are shallow mounted solids, not floating cards.
        for longitudinal, material in ((-2.78, "LIGHT_RED"), (-0.12, "LIGHT_BLUE")):
            y0, y1 = longitudinal, longitudinal + 0.25
            lo_x, hi_x = sorted((side * 1.08, side * 1.105))
            builder.box("SideWarning" + str((side, longitudinal)),
                        (lo_x, y0, 2.14), (hi_x, y1, 2.34), material)

    # Service-compartment seams and a dedicated emblem panel on each side.
    for y in (-2.18, -1.26):
        side_panel(builder, "ServiceSeam" + str(y), 1,
                   [(y, 1.53), (y + 0.018, 1.53),
                    (y + 0.018, 2.28), (y, 2.28)], "SEAM", 1.089)
    for side in (-1, 1):
        side_panel(builder, "MedicalSide" + str(side), side,
                   [(-1.95, 1.58), (-1.15, 1.58), (-1.15, 2.32), (-1.95, 2.32)],
                   "MEDICAL", 1.091)
        side_panel(builder, "EmsSide" + str(side), side,
                   [(-2.77, 1.53), (-2.02, 1.53), (-2.02, 1.78), (-2.77, 1.78)],
                   "EMS_TEXT", 1.092)

    build_rear(builder)


def build_rear(builder):
    rear_y = -3.00
    # Door surrounds leave two actual glass apertures in the rear wall.
    builder.box("RearLower", (-1.08, rear_y - 0.06, 0.36),
                (1.08, rear_y, 1.34), "REAR_DOOR")
    builder.box("RearTop", (-1.08, rear_y - 0.06, 2.16),
                (1.08, rear_y, 2.44), "REAR_DOOR")
    for side in (-1, 1):
        x0, x1 = sorted((side * 0.74, side * 1.08))
        builder.box("RearOuter" + str(side), (x0, rear_y - 0.06, 1.34),
                    (x1, rear_y, 2.16), "REAR_DOOR")
    builder.box("RearCentrePillar", (-0.07, rear_y - 0.06, 1.34),
                (0.07, rear_y, 2.16), "REAR_DOOR")
    for side in (-1, 1):
        x0, x1 = sorted((side * 0.66, side * 0.13))
        builder.panel("RearGlass" + str(side), [
            (x0, rear_y - 0.065, 1.43), (x1, rear_y - 0.065, 1.43),
            (x1, rear_y - 0.065, 2.07), (x0, rear_y - 0.065, 2.07),
        ], "GLASS", (0, -1, 0))
    builder.panel("RearMedical", [
        (-0.34, rear_y - 0.066, 0.57), (0.34, rear_y - 0.066, 0.57),
        (0.34, rear_y - 0.066, 1.20), (-0.34, rear_y - 0.066, 1.20),
    ], "MEDICAL", (0, -1, 0))
    builder.panel("RearStripe", [
        (-1.04, rear_y - 0.067, 1.12), (1.04, rear_y - 0.067, 1.12),
        (1.04, rear_y - 0.067, 1.32), (-1.04, rear_y - 0.067, 1.32),
    ], "STRIPE", (0, -1, 0))
    for side in (-1, 1):
        x0, x1 = sorted((side * 0.84, side * 1.02))
        for material, z0, z1 in (
            ("TAIL_RED", 0.66, 0.82),
            ("REVERSE", 0.83, 0.95),
            ("TAIL_RED", 0.96, 1.13),
        ):
            builder.box(material + "Rear" + str((side, z0)),
                        (x0, rear_y - 0.075, z0),
                        (x1, rear_y - 0.058, z1), material)
        warning = "LIGHT_RED" if side < 0 else "LIGHT_BLUE"
        builder.box("RearWarning" + str(side),
                    (x0, rear_y - 0.075, 2.21),
                    (x1, rear_y - 0.058, 2.39), warning)


def build_front(builder):
    # Nose, bumper and flush-mounted fascia elements.
    builder.loft("Hood", [
        (1.06, [(-0.88, 1.12), (-0.88, 1.22), (0.88, 1.22), (0.88, 1.12)]),
        (2.22, [(-0.92, 1.02), (-0.88, 1.16), (0.88, 1.16), (0.92, 1.02)]),
        (2.96, [(-0.88, 0.93), (-0.82, 1.08), (0.82, 1.08), (0.88, 0.93)]),
    ], "CAB_BODY")
    builder.box("FrontFace", (-0.89, 2.94, 0.42), (0.89, 3.00, 1.04), "CAB_BODY")
    builder.box("FrontBumper", (-1.08, 2.93, 0.31),
                (1.08, 3.06, 0.46), "METAL")
    builder.box("Grille", (-0.52, 3.00, 0.58), (0.52, 3.025, 0.94), "GRILLE")
    for side in (-1, 1):
        x0, x1 = sorted((side * 0.59, side * 0.91))
        builder.box("Headlight" + str(side), (x0, 3.00, 0.69),
                    (x1, 3.025, 0.93), "HEADLIGHT")
        marker_x0, marker_x1 = sorted((side * 0.91, side * 1.02))
        builder.box("FrontMarker" + str(side), (marker_x0, 2.98, 0.69),
                    (marker_x1, 3.015, 0.86), "AMBER")


def build_warning_front(builder):
    # Front-facing module lamps sit against its upper face with a solid mount.
    for side in (-1, 1):
        x0, x1 = sorted((side * 0.72, side * 0.99))
        material = "LIGHT_RED" if side < 0 else "LIGHT_BLUE"
        builder.box("FrontWarning" + str(side), (x0, 0.20, 2.19),
                    (x1, 0.225, 2.39), material)
    builder.box("SceneLight", (-0.25, 0.20, 2.22),
                (0.25, 0.224, 2.38), "HEADLIGHT")


def build_vehicle():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    builder = AmbulanceBuilder()
    builder.box("Chassis", (-0.52, -2.94, 0.24),
                (0.52, 2.94, 0.39), "SHADOW")
    build_cab(builder)
    build_module(builder)
    build_front(builder)
    build_warning_front(builder)
    for side in (-1, 1):
        fender(builder, side, WHEELS["front_z"], True)
        fender(builder, side, WHEELS["rear_z"], False)

    for obj in builder.objects:
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
        bmesh.ops.dissolve_degenerate(bm, edges=list(bm.edges), dist=1e-7)
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()
        map_object(obj)

    body = builder.join()
    body.data.name = "MunicipalAmbulanceBody"
    for side in (-1, 1):
        for axle in (WHEELS["front_z"], WHEELS["rear_z"]):
            empty = bpy.data.objects.new("WHEEL_" + str((side, axle)), None)
            empty.empty_display_type = "SPHERE"
            empty.empty_display_size = 0.12
            empty.location = (side * WHEELS["x"], axle, WHEELS["arch_y"])
            bpy.context.collection.objects.link(empty)
    assert len([obj for obj in bpy.context.scene.objects if obj.type == "MESH"]) == 1
    return body


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesh", type=Path, required=True)
    parser.add_argument("--blend", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    body = build_vehicle()
    args.blend.parent.mkdir(parents=True, exist_ok=True)
    texture_path = (Path(__file__).resolve().parents[1] /
                    "assets/textures/vehicles/ambulance/body.png")
    atlas = bpy.data.images.load(str(texture_path), check_existing=True)
    atlas.filepath = bpy.path.relpath(str(texture_path), start=str(args.blend.parent))
    for slot in body.material_slots:
        material = slot.material
        material.use_nodes = True
        shader = material.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Roughness"].default_value = 1.0
        texture = material.node_tree.nodes.new("ShaderNodeTexImage")
        texture.image = atlas
        texture.interpolation = "Closest"
        material.node_tree.links.new(texture.outputs["Color"], shader.inputs["Base Color"])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    vertices, triangles = export_emesh(body, args.mesh, "ambulance")
    print(f"MUNICIPAL_AMBULANCE body=BODY vertices={vertices} triangles={triangles} "
          f"blend={args.blend} mesh={args.mesh}")


if __name__ == "__main__":
    main()
