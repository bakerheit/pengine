#!/usr/bin/env python3
"""Blender cooker for the original rounded Spagatti Shū hypercar."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from spagatti_shu_spec import (ATLAS_SIZE, REGIONS, UV_PROJECTIONS,
                               WHEEL_ANCHORS)
from vesper_vx91_blender import VehicleBuilder, export_emesh


def body_ring(width: float, shoulder: float, deck: float,
              crown: float) -> list[tuple[float, float]]:
    """Twelve-sided section with a rounded shoulder and broad centre panel."""
    return [
        (-width * .62, .16), (-width * .90, .22), (-width, .38),
        (-width, shoulder), (-width * .76, deck), (-width * .30, crown),
        (width * .30, crown), (width * .76, deck), (width, shoulder),
        (width, .38), (width * .90, .22), (width * .62, .16),
    ]


def side_panel(builder: VehicleBuilder, name: str, side: float,
               yz: list[tuple[float, float]], material: str,
               x: float) -> None:
    builder.panel(name, [(side * x, y, z) for y, z in yz], material,
                  (side, 0, 0))


def beam(builder: VehicleBuilder, name: str, start: tuple[float, float, float],
         end: tuple[float, float, float], width: float,
         material: str) -> bpy.types.Object:
    """Capped square-section frame member between arbitrary 3D points."""
    direction = (Vector(end) - Vector(start)).normalized()
    reference = Vector((0, 0, 1)) if abs(direction.z) < .9 else Vector((1, 0, 0))
    across = direction.cross(reference).normalized() * width * .5
    depth = direction.cross(across).normalized() * width * .5
    section = (across, -across * .5 + depth * .866,
               -across * .5 - depth * .866)
    vertices = [tuple(Vector(point) + offset)
                for point in (start, end) for offset in section]
    return builder.add_mesh(name, vertices,
                            [(0, 2, 1), (3, 4, 5),
                             (0, 1, 4, 3), (1, 2, 5, 4),
                             (2, 0, 3, 5)], material)


def closed_pane(builder: VehicleBuilder, name: str,
                points: list[tuple[float, float, float]], material: str,
                outward: tuple[float, float, float],
                thickness: float = .012) -> bpy.types.Object:
    """Thin two-sided glazing whose edge belongs inside a solid frame."""
    desired = Vector(outward).normalized()
    front = [Vector(point) for point in points]
    if (front[1] - front[0]).cross(front[2] - front[0]).dot(desired) < 0:
        front.reverse()
    back = [point - desired * thickness for point in front]
    vertices = [tuple(point) for point in front + back]
    count = len(front)
    faces = [tuple(range(count)), tuple(reversed(range(count, count * 2)))]
    faces.extend((index, (index + 1) % count,
                  count + (index + 1) % count, count + index)
                 for index in range(count))
    return builder.add_mesh(name, vertices, faces, material)


def upper_arch_lip(builder: VehicleBuilder, name: str, side: float,
                   centre_y: float) -> None:
    """Broad haunch grown inward from each real wheel opening."""
    vertices = []
    segments = 6
    section = (
        (side * 1.075, .50), (side * .97, .56), (side * .76, .52),
        (side * .69, .46), (side * .76, .485), (side * 1.075, .455),
    )
    for index in range(segments + 1):
        angle = math.pi / 7 + 5 * math.pi / 7 * index / segments
        for x, radius in section:
            vertices.append((x, centre_y + math.cos(angle) * radius,
                             .43 + math.sin(angle) * radius))
    faces = []
    for index in range(segments):
        a = index * len(section)
        b = (index + 1) * len(section)
        for j in range(len(section)):
            following = (j + 1) % len(section)
            faces.append((a + j, b + j, b + following, a + following))
    faces.append(tuple(reversed(range(len(section)))))
    faces.append(tuple(segments * len(section) + j for j in range(len(section))))
    if side < 0:
        faces = [tuple(reversed(face)) for face in faces]
    builder.add_mesh(name, vertices, faces, "BODY_SIDE")


def cut_wheel_wells(solids: tuple[bpy.types.Object, ...]) -> None:
    """Cut four side-only pockets while keeping the centre hood and deck."""
    for solid in solids:
        for label, centre_y in (("Front", WHEEL_ANCHORS["front_z"]),
                                ("Rear", WHEEL_ANCHORS["rear_z"])):
            for side, suffix in ((-1., "L"), (1., "R")):
                segments = 10
                x_inner = side * (.62 if solid.name == "SculptedBody" else .54)
                x_outer = side * 1.32
                vertices = []
                for x in sorted((x_inner, x_outer)):
                    for index in range(segments):
                        angle = math.tau * index / segments
                        vertices.append((
                            x,
                            centre_y + math.cos(angle) * .53,
                            WHEEL_ANCHORS["arch_y"] + math.sin(angle) * .47,
                        ))
                faces = []
                for index in range(segments):
                    nxt = (index + 1) % segments
                    faces.append((index, nxt, segments + nxt, segments + index))
                faces.append(tuple(reversed(range(segments))))
                faces.append(tuple(segments + index for index in range(segments)))
                cutter_name = f"{label}{suffix}WheelWellCutter"
                mesh = bpy.data.meshes.new(cutter_name + "Mesh")
                mesh.from_pydata(vertices, [], faces)
                mesh.validate(verbose=False)
                mesh.update()
                cutter = bpy.data.objects.new(cutter_name, mesh)
                bpy.context.collection.objects.link(cutter)
                modifier = solid.modifiers.new("Cut" + cutter_name, "BOOLEAN")
                modifier.operation = "DIFFERENCE"
                modifier.solver = "EXACT"
                modifier.object = cutter
                bpy.context.view_layer.objects.active = solid
                solid.select_set(True)
                bpy.ops.object.modifier_apply(modifier=modifier.name)
                bpy.data.objects.remove(cutter, do_unlink=True)
        for polygon in solid.data.polygons:
            polygon.use_smooth = False


def assign_uvs(body: bpy.types.Object) -> None:
    """Project the final mesh into model-sized semantic atlas receivers."""
    mesh = body.data
    uv_layer = mesh.uv_layers.new(name="UVMap")

    def axes_for(polygon) -> tuple[int, int]:
        normal = polygon.normal
        if abs(normal.z) >= abs(normal.x) and abs(normal.z) >= abs(normal.y):
            return 0, 1
        if abs(normal.y) >= abs(normal.x):
            return 0, 2
        return 1, 2

    for polygon in mesh.polygons:
        material = body.material_slots[polygon.material_index].material.name
        x0, y0, x1, y1 = REGIONS[material]
        u0, u1 = (x0 + 2) / ATLAS_SIZE, (x1 - 2) / ATLAS_SIZE
        v0, v1 = 1 - (y1 - 2) / ATLAS_SIZE, 1 - (y0 + 2) / ATLAS_SIZE
        coordinates = [mesh.vertices[mesh.loops[i].vertex_index].co
                       for i in polygon.loop_indices]
        if material in UV_PROJECTIONS:
            axes, pairs = UV_PROJECTIONS[material]
            bounds = (pairs[0][0], pairs[0][1], pairs[1][0], pairs[1][1])
        else:
            axes = axes_for(polygon)
            bounds = (min(c[axes[0]] for c in coordinates),
                      max(c[axes[0]] for c in coordinates),
                      min(c[axes[1]] for c in coordinates),
                      max(c[axes[1]] for c in coordinates))
        span_a, span_b = bounds[1] - bounds[0], bounds[3] - bounds[2]
        for loop, coordinate in zip(polygon.loop_indices, coordinates):
            a = .5 if span_a < 1e-6 else (coordinate[axes[0]] - bounds[0]) / span_a
            b = .5 if span_b < 1e-6 else (coordinate[axes[1]] - bounds[2]) / span_b
            uv_layer.data[loop].uv = (u0 + a * (u1 - u0),
                                      v0 + b * (v1 - v0))


def build_vehicle() -> bpy.types.Object:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    builder = VehicleBuilder()
    chassis = builder.box("ChassisCore", (-.61, -2.22, .22),
                          (.61, 2.20, .48), "BODY_SHADOW")
    floor = builder.box("FlatFloor", (-.72, -2.15, .16),
                        (.72, 2.12, .25), "CLADDING")

    # Independent Spagatti silhouette: short rounded prow, high centre arch,
    # and rear-biased haunches. No GLM stations or targa geometry are reused.
    shell = builder.loft("SculptedBody", [
        (-2.40, body_ring(.74, .43, .53, .57)),
        (-2.24, body_ring(.96, .66, .75, .78)),
        (-1.86, body_ring(1.06, .75, .85, .88)),
        (-1.35, body_ring(1.08, .78, .89, .92)),
        (-.88, body_ring(1.04, .75, .84, .87)),
        (-.24, body_ring(.98, .70, .76, .79)),
        (.42, body_ring(.99, .68, .73, .76)),
        (.92, body_ring(1.04, .72, .80, .84)),
        (1.45, body_ring(1.07, .76, .87, .91)),
        (1.82, body_ring(1.02, .66, .76, .80)),
        (2.15, body_ring(.89, .49, .57, .61)),
        (2.40, body_ring(.58, .32, .38, .41)),
    ], "BODY_TOP")
    shell.data.materials.append(builder.material("BODY_SIDE"))
    shell.data.materials.append(builder.material("BODY_SHADOW"))
    shell.data.materials.append(builder.material("BODY_FRONT"))
    shell.data.materials.append(builder.material("BODY_REAR"))
    for polygon in shell.data.polygons:
        points = [shell.data.vertices[index].co for index in polygon.vertices]
        # The nose and tail are curved depth fields, not flat end plates. Give
        # every forward/rear-facing facet near an end the same physical X/Z
        # projection so painted details remain flush around the curvature.
        if max(point.y for point in points) > 1.80 and polygon.normal.y > .15:
            polygon.material_index = 3
        elif min(point.y for point in points) < -1.80 and polygon.normal.y < -.15:
            polygon.material_index = 4
        elif polygon.normal.z < -.10:
            polygon.material_index = 2
        elif polygon.normal.z < .58:
            polygon.material_index = 1

    # The greenhouse is a real framed opening. The old version lofted a solid
    # painted cabin and placed window cards over it, which exposed red body
    # beneath the glass and made every pane float on a different plane.
    builder.loft("RoofCap", [
        (-.43, [(-.52, 1.155), (-.45, 1.215),
                 (.45, 1.215), (.52, 1.155)]),
        (-.10, [(-.54, 1.165), (-.46, 1.225),
                 (.46, 1.225), (.54, 1.165)]),
        (.18, [(-.52, 1.155), (-.44, 1.215),
                (.44, 1.215), (.52, 1.155)]),
    ], "BODY_TOP")

    windshield_outer = [
        (-.70, .84, .82), (.70, .84, .82),
        (.52, .18, 1.205), (-.52, .18, 1.205),
    ]
    windshield_glass = [
        (-.64, .80, .87), (.64, .80, .87),
        (.47, .225, 1.16), (-.47, .225, 1.16),
    ]
    rear_outer = [
        (.52, -.43, 1.195), (-.52, -.43, 1.195),
        (-.70, -1.08, .84), (.70, -1.08, .84),
    ]
    rear_glass = [
        (.47, -.465, 1.155), (-.47, -.465, 1.155),
        (-.64, -1.035, .89), (.64, -1.035, .89),
    ]
    for name, corners in (("WindshieldFrame", windshield_outer),
                          ("RearWindowFrame", rear_outer)):
        for index in range(4):
            # The body shell is already the lower cowl/deck edge. Repeating a
            # beam there would stack geometry on the sill instead of framing
            # the optical opening.
            if (name == "WindshieldFrame" and index == 0) or \
                    (name == "RearWindowFrame" and index == 2):
                continue
            beam(builder, name + str(index), corners[index],
                 corners[(index + 1) % 4], .055, "BODY_SIDE")
    closed_pane(builder, "Windshield", windshield_glass, "GLASS_FRONT",
                (0, 1, .5))
    closed_pane(builder, "RearGlass", rear_glass, "GLASS_REAR",
                (0, -1, .5))

    for side in (-1., 1.):
        suffix = "L" if side < 0 else "R"
        upper_arch_lip(builder, "FrontHaunch" + suffix, side, 1.45)
        upper_arch_lip(builder, "RearHaunch" + suffix, side, -1.35)
        front_low = (side * .70, .80, .83)
        front_top = (side * .52, .18, 1.205)
        rear_top = (side * .52, -.43, 1.195)
        divider_low = (side * .70, -.55, .85)
        rear_low = (side * .70, -1.08, .84)
        for name, start, end in (
            ("APillar", front_low, front_top),
            ("RoofRail", front_top, rear_top),
            ("CPillar", rear_top, rear_low),
            ("WindowDivider", rear_top, divider_low),
            ("DoorWindowSill", front_low, divider_low),
            ("QuarterWindowSill", divider_low, rear_low),
        ):
            beam(builder, name + suffix, start, end, .050, "BODY_SIDE")
        closed_pane(builder, "DoorGlass" + suffix, [
            (side * .695, .745, .865),
            (side * .695, -.505, .875),
            (side * .515, -.395, 1.155),
            (side * .505, .215, 1.165),
        ], "GLASS_SIDE", (side, 0, 0))
        closed_pane(builder, "QuarterGlass" + suffix, [
            (side * .695, -.595, .875),
            (side * .665, -1.005, .885),
            (side * .535, -.755, 1.035),
            (side * .515, -.455, 1.145),
        ], "GLASS_SIDE", (side, 0, 0))
        side_panel(builder, "SideIntake" + suffix, side, [
            (-1.12, .43), (-.29, .47), (-.48, .71), (-1.00, .67),
        ], "BLACK", 1.083)
        side_panel(builder, "IntakeBlade" + suffix, side, [
            (-1.02, .50), (-.48, .52), (-.55, .58), (-.94, .58),
        ], "SEAM", 1.086)
        side_panel(builder, "Rocker" + suffix, side, [
            (-.83, .25), (.87, .25), (.76, .36), (-.75, .38),
        ], "CLADDING", 1.055)
        side_panel(builder, "DoorSeam" + suffix, side, [
            (.69, .39), (.72, .40), (.68, .82), (.65, .82),
        ], "LENS_DARK", 1.027)
        side_panel(builder, "Handle" + suffix, side, [
            (-.24, .76), (.02, .76), (.02, .80), (-.24, .80),
        ], "METAL", 1.035)
        x0, x1 = sorted((side * .80, side * .96))
        builder.box("Mirror" + suffix, (x0, .48, .85),
                    (x1, .67, .94), "BLACK")

    # Grille and lamp cells are painted directly on these curved end facets.
    # Runtime glow repeats the exact body triangles, so no offset cards belong
    # here. The splitter/diffuser retain geometry because they change silhouette.
    builder.box("FrontSplitter", (-1.02, 2.35, .16),
                (1.02, 2.42, .24), "CLADDING")
    builder.box("RearDiffuser", (-1.00, -2.42, .16),
                (1.00, -2.34, .27), "CLADDING")

    cut_wheel_wells((chassis, floor, shell))
    body = builder.join()
    body.data.name = "SpagattiShuBody"
    assign_uvs(body)
    for name, location in {
        "WHEEL_FL": (-.94, 1.45, .43), "WHEEL_FR": (.94, 1.45, .43),
        "WHEEL_RL": (-.94, -1.35, .43), "WHEEL_RR": (.94, -1.35, .43),
    }.items():
        empty = bpy.data.objects.new(name, None)
        empty.empty_display_type = "SPHERE"
        empty.empty_display_size = .11
        empty.location = location
        bpy.context.collection.objects.link(empty)
    return body


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
    vertices, triangles = export_emesh(body, args.mesh, "spagatti_shu")
    print(f"SPAGATTI_SHU_BLENDER vertices={vertices} triangles={triangles} "
          f"blend={args.blend} mesh={args.mesh}")


if __name__ == "__main__":
    main()
