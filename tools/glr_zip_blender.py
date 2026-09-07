#!/usr/bin/env python3
"""Blender source builder for the compact, targa-roofed GLR ZIP."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from glr_zip_spec import ATLAS_SIZE, REGIONS
from vesper_vx91_blender import VehicleBuilder, export_emesh


def body_ring(width: float, shoulder: float,
              deck: float) -> list[tuple[float, float]]:
    """Ten-sided section with a soft lower tuck and crisp exotic shoulder."""
    return [
        (-width * 0.70, 0.20), (-width * 0.94, 0.28),
        (-width, 0.43), (-width, shoulder),
        (-width * 0.72, deck), (width * 0.72, deck),
        (width, shoulder), (width, 0.43),
        (width * 0.94, 0.28), (width * 0.70, 0.20),
    ]


def cabin_ring(shoulder: float, roof_width: float,
               roof_z: float) -> list[tuple[float, float]]:
    """Low faceted targa section; wide enough to look usable, never van-like."""
    return [
        (-shoulder, 0.78), (-shoulder * 0.98, 0.91),
        (-roof_width, roof_z - 0.08), (-roof_width * 0.90, roof_z),
        (roof_width * 0.90, roof_z), (roof_width, roof_z - 0.08),
        (shoulder * 0.98, 0.91), (shoulder, 0.78),
    ]


def side_panel(builder: VehicleBuilder, name: str, side: float,
               yz: list[tuple[float, float]], material: str,
               x: float) -> None:
    builder.panel(name, [(side * x, y, z) for y, z in yz], material,
                  (side, 0.0, 0.0))


def upper_arch_lip(builder: VehicleBuilder, name: str, side: float,
                   centre_y: float) -> None:
    """Broad faceted crown that rolls inward and down onto the deck."""
    vertices: list[tuple[float, float, float]] = []
    segments = 6
    landing_radius = 0.42 if centre_y > 0.0 else 0.50
    # Outer edge, raised crown, hood/deck landing, then a shallow underside.
    # The changing radius makes the strip slope down as it moves inward.
    section = (
        (side * 1.145, 0.49),
        (side * 0.98, 0.56),
        (side * 0.72, landing_radius),
        (side * 0.72, landing_radius - 0.035),
        (side * 1.145, 0.455),
    )
    for index in range(segments + 1):
        angle = math.pi / 6.0 + (2.0 * math.pi / 3.0) * index / segments
        for x, radius in section:
            vertices.append((x, centre_y + math.cos(angle) * radius,
                             0.46 + math.sin(angle) * radius))
    faces: list[tuple[int, ...]] = []
    for index in range(segments):
        a = index * len(section)
        b = (index + 1) * len(section)
        for section_index in range(len(section)):
            following = (section_index + 1) % len(section)
            faces.append((a + section_index, b + section_index,
                          b + following, a + following))
    faces.extend((tuple(reversed(range(len(section)))),
                  tuple(segments * len(section) + index
                        for index in range(len(section)))))
    if side < 0.0:
        faces = [tuple(reversed(face)) for face in faces]
    builder.add_mesh(name, vertices, faces, "BODY_SIDE")


def assign_zip_uvs(body: bpy.types.Object) -> None:
    """Map broad panels continuously and give every small detail its atlas cell."""
    mesh = body.data
    uv_layer = mesh.uv_layers.new(name="UVMap")
    detail_materials = {
        "BLACK", "DASH", "EXHAUST", "GLASS", "HEADLIGHT", "INTERIOR",
        "LENS_DARK", "MARKER_AMBER", "METAL", "REVERSE", "SEAM",
        "TAIL_AMBER", "TAIL_RED",
    }

    def axes_for(polygon) -> tuple[int, int]:
        normal = polygon.normal
        if abs(normal.z) >= abs(normal.x) and abs(normal.z) >= abs(normal.y):
            return 0, 1
        if abs(normal.y) >= abs(normal.x):
            return 0, 2
        return 1, 2

    projected_bounds: dict[tuple[str, tuple[int, int]], tuple[float, ...]] = {}
    for polygon in mesh.polygons:
        material = body.material_slots[polygon.material_index].material.name
        if material in detail_materials:
            continue
        axes = axes_for(polygon)
        key = material, axes
        values = projected_bounds.get(key)
        coordinates = [
            mesh.vertices[mesh.loops[loop].vertex_index].co
            for loop in polygon.loop_indices
        ]
        a_values = [co[axes[0]] for co in coordinates]
        b_values = [co[axes[1]] for co in coordinates]
        current = (min(a_values), max(a_values), min(b_values), max(b_values))
        if values is None:
            projected_bounds[key] = current
        else:
            projected_bounds[key] = (
                min(values[0], current[0]), max(values[1], current[1]),
                min(values[2], current[2]), max(values[3], current[3]),
            )

    for polygon in mesh.polygons:
        material = body.material_slots[polygon.material_index].material.name
        x0, y0, x1, y1 = REGIONS[material]
        u0, u1 = (x0 + 2) / ATLAS_SIZE, (x1 - 2) / ATLAS_SIZE
        v0, v1 = 1.0 - (y1 - 2) / ATLAS_SIZE, 1.0 - (y0 + 2) / ATLAS_SIZE
        axes = axes_for(polygon)
        coordinates = [
            mesh.vertices[mesh.loops[loop].vertex_index].co
            for loop in polygon.loop_indices
        ]
        if material in detail_materials:
            a_values = [co[axes[0]] for co in coordinates]
            b_values = [co[axes[1]] for co in coordinates]
            bounds = (min(a_values), max(a_values),
                      min(b_values), max(b_values))
        else:
            bounds = projected_bounds[material, axes]
        a_span = bounds[1] - bounds[0]
        b_span = bounds[3] - bounds[2]
        for loop_index, co in zip(polygon.loop_indices, coordinates):
            a = 0.5 if a_span < 1e-6 else (co[axes[0]] - bounds[0]) / a_span
            b = 0.5 if b_span < 1e-6 else (co[axes[1]] - bounds[2]) / b_span
            uv_layer.data[loop_index].uv = (
                u0 + a * (u1 - u0), v0 + b * (v1 - v0),
            )


def cut_wheel_wells(solids: tuple[bpy.types.Object, ...]) -> None:
    """Boolean four side pockets while preserving the centre hood and spine."""
    segments = 12
    for solid in solids:
        for label, centre_y, long_radius in (
            ("Front", 1.65, 0.56), ("Rear", -1.60, 0.53),
        ):
            for side, suffix in ((-1.0, "L"), (1.0, "R")):
                # Stop well inboard of the wheel, but never cross the centre.
                # The old full-width cutter made a clean side opening by also
                # deleting the hood directly above both axle lines.
                # Let the visible shell keep a wider hood/deck. The buried
                # chassis and floor still get deeper clearance for full-lock
                # steering, including the sharper development presets.
                inner_depth = 0.70 if solid.name == "SculptedBody" else 0.56
                x_inner = side * inner_depth
                x_outer = side * 1.40
                vertices: list[tuple[float, float, float]] = []
                # Keep the prism winding identical on the left and right;
                # reversed left-side end caps can make Blender's Boolean skip
                # the outer skin while still cutting the other solids.
                for x in sorted((x_inner, x_outer)):
                    for index in range(segments):
                        angle = math.tau * index / segments
                        vertices.append((
                            x,
                            centre_y + math.cos(angle) * long_radius,
                            0.46 + math.sin(angle) * 0.50,
                        ))
                faces: list[tuple[int, ...]] = []
                for index in range(segments):
                    following = (index + 1) % segments
                    faces.append((index, following,
                                  segments + following, segments + index))
                faces.append(tuple(reversed(range(segments))))
                faces.append(tuple(
                    segments + index for index in range(segments)))

                cutter_name = f"{label}{suffix}WheelWellCutter"
                mesh = bpy.data.meshes.new(f"{cutter_name}Mesh")
                mesh.from_pydata(vertices, [], faces)
                mesh.validate(verbose=False)
                mesh.update()
                cutter = bpy.data.objects.new(cutter_name, mesh)
                bpy.context.collection.objects.link(cutter)

                modifier = solid.modifiers.new(
                    f"Cut{label}{suffix}WheelWell", "BOOLEAN")
                modifier.operation = "DIFFERENCE"
                modifier.solver = "EXACT"
                modifier.object = cutter
                bpy.context.view_layer.objects.active = solid
                solid.select_set(True)
                bpy.ops.object.modifier_apply(modifier=modifier.name)
                bpy.data.objects.remove(cutter, do_unlink=True)

        for polygon in solid.data.polygons:
            polygon.use_smooth = False


def build_vehicle():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    builder = VehicleBuilder()

    # Longer wheelbase and a lower datum fix the original toy-car stance.
    chassis = builder.box("ChassisCore", (-0.64, -2.38, 0.25),
                          (0.64, 2.38, 0.50), "BODY_SHADOW")
    floor = builder.box("FlatFloor", (-0.74, -2.29, 0.18),
                        (0.74, 2.31, 0.27), "CLADDING")

    # More stations and a chamfered ten-sided section give the main shell a
    # continuous, hand-shaped flow. The tail has the mass; the nose is low.
    shell = builder.loft("SculptedBody", [
        (-2.50, body_ring(0.88, 0.54, 0.62)),
        (-2.34, body_ring(1.02, 0.68, 0.78)),
        (-2.05, body_ring(1.11, 0.77, 0.90)),
        (-1.60, body_ring(1.15, 0.83, 0.96)),
        (-1.28, body_ring(1.14, 0.82, 0.93)),
        (-0.74, body_ring(1.04, 0.76, 0.85)),
        (-0.10, body_ring(0.99, 0.72, 0.80)),
        (0.48, body_ring(1.00, 0.69, 0.77)),
        (0.93, body_ring(1.06, 0.68, 0.74)),
        (1.30, body_ring(1.11, 0.72, 0.82)),
        (1.65, body_ring(1.13, 0.76, 0.88)),
        (1.95, body_ring(1.09, 0.65, 0.74)),
        (2.36, body_ring(0.96, 0.46, 0.54)),
        (2.50, body_ring(0.82, 0.39, 0.47)),
    ], "BODY_TOP")
    shell.data.materials.append(builder.material("BODY_SIDE"))
    shell.data.materials.append(builder.material("BODY_SHADOW"))
    for polygon in shell.data.polygons:
        if polygon.normal.z < -0.15:
            polygon.material_index = 2
        elif polygon.normal.z < 0.55:
            polygon.material_index = 1

    # Lower, tapered glass fixes the old upright toy cabin. The black panel
    # and structural hoop keep the ZIP's targa identity.
    builder.loft("TaperedCabin", [
        (-1.00, cabin_ring(0.76, 0.56, 0.98)),
        (-0.68, cabin_ring(0.83, 0.59, 1.24)),
        (-0.38, cabin_ring(0.84, 0.60, 1.30)),
        (0.33, cabin_ring(0.83, 0.59, 1.30)),
        (0.55, cabin_ring(0.80, 0.56, 1.24)),
        (1.05, cabin_ring(0.70, 0.48, 0.98)),
    ], "BODY_SIDE")

    for side in (-1.0, 1.0):
        suffix = "L" if side < 0 else "R"

        upper_arch_lip(builder, f"FrontArch{suffix}", side, 1.65)
        upper_arch_lip(builder, f"RearArch{suffix}", side, -1.60)

        # Tapered panels replace the old square rocker and fender blocks.
        side_panel(builder, f"RockerBlade{suffix}", side, [
            (-0.99, 0.27), (0.99, 0.27), (0.89, 0.38),
            (-0.87, 0.40),
        ], "CLADDING", 1.106)
        side_panel(builder, f"DoorGlass{suffix}", side, [
            (1.00, 0.84), (0.37, 1.22), (-0.37, 1.24),
            (-0.72, 0.86),
        ], "GLASS", 0.867)
        side_panel(builder, f"QuarterGlass{suffix}", side, [
            (-0.76, 0.86), (-0.42, 1.23), (-0.61, 1.20),
            (-0.95, 0.88),
        ], "GLASS", 0.853)
        side_panel(builder, f"SweptIntake{suffix}", side, [
            (-1.16, 0.45), (-0.25, 0.47), (-0.39, 0.69),
            (-1.01, 0.64),
        ], "BLACK", 1.148)
        side_panel(builder, f"IntakeHighlight{suffix}", side, [
            (-1.01, 0.51), (-0.42, 0.52), (-0.48, 0.57),
            (-0.94, 0.57),
        ], "BODY_SHADOW", 1.150)
        side_panel(builder, f"DoorFrontSeam{suffix}", side, [
            (0.76, 0.42), (0.79, 0.43), (0.72, 0.86),
            (0.69, 0.86),
        ], "SEAM", 1.024)
        side_panel(builder, f"DoorRearSeam{suffix}", side, [
            (-0.54, 0.43), (-0.51, 0.43), (-0.43, 0.87),
            (-0.46, 0.87),
        ], "SEAM", 1.028)
        side_panel(builder, f"DoorSillSeam{suffix}", side, [
            (-0.53, 0.42), (0.77, 0.42), (0.77, 0.445),
            (-0.53, 0.445),
        ], "SEAM", 1.030)
        side_panel(builder, f"ShoulderCrease{suffix}", side, [
            (-1.10, 0.72), (0.60, 0.68), (0.60, 0.715),
            (-1.10, 0.755),
        ], "BODY_SHADOW", 1.084)
        side_panel(builder, f"FlushHandle{suffix}", side, [
            (-0.27, 0.78), (0.02, 0.78), (0.02, 0.825),
            (-0.27, 0.825),
        ], "METAL", 1.087)
        side_panel(builder, f"FrontMarker{suffix}", side, [
            (1.94, 0.49), (2.09, 0.49), (2.09, 0.555),
            (1.94, 0.555),
        ], "MARKER_AMBER", 1.074)

        x0, x1 = ((-1.01, -0.89) if side < 0 else (0.89, 1.01))
        builder.box(f"Mirror{suffix}", (x0, 0.50, 0.84),
                    (x1, 0.65, 0.93), "BLACK")

    builder.panel("RakedWindshield", [
        (-0.71, 1.015, 0.83), (0.71, 1.015, 0.83),
        (0.56, 0.425, 1.245), (-0.56, 0.425, 1.245),
    ], "GLASS", (0.0, 1.0, 0.5))
    builder.panel("SlopedRearGlass", [
        (0.56, -0.535, 1.24), (-0.56, -0.535, 1.24),
        (-0.71, -0.995, 0.89), (0.71, -0.995, 0.89),
    ], "GLASS", (0.0, -1.0, 0.35))
    builder.box("BlackTargaRoof", (-0.58, -0.52, 1.273),
                (0.58, 0.42, 1.305), "BLACK")
    builder.loft("TargaHoop", [
        (-0.67, [(-0.68, 1.10), (-0.61, 1.295),
                 (0.61, 1.295), (0.68, 1.10)]),
        (-0.57, [(-0.65, 1.12), (-0.59, 1.300),
                 (0.59, 1.300), (0.65, 1.12)]),
    ], "BODY_SIDE")
    builder.box("Dashboard", (-0.58, 0.50, 0.81),
                (0.58, 0.61, 0.91), "DASH")
    builder.box("SeatLeft", (-0.53, -0.25, 0.78),
                (-0.10, 0.01, 1.02), "INTERIOR")
    builder.box("SeatRight", (0.10, -0.25, 0.78),
                (0.53, 0.01, 1.02), "INTERIOR")

    # Four inset lamps form a narrow 1980s rally-car face without pop-ups.
    for side in (-1.0, 1.0):
        x0, x1 = sorted((side * 0.30, side * 0.94))
        builder.panel(f"LampRecess{side}", [
            (x0, 2.507, 0.39), (x1, 2.507, 0.39),
            (x1 * 0.97, 2.507, 0.58), (x0 * 0.97, 2.507, 0.58),
        ], "BLACK", (0.0, 1.0, 0.0))
    for index, (x0, x1) in enumerate((
        (-0.89, -0.64), (-0.58, -0.33),
        (0.33, 0.58), (0.64, 0.89),
    )):
        builder.panel(f"InsetHeadlamp{index}", [
            (x0, 2.510, 0.435), (x1, 2.510, 0.435),
            (x1, 2.510, 0.525), (x0, 2.510, 0.525),
        ], "HEADLIGHT", (0.0, 1.0, 0.0))
    builder.box("FrontSplitter", (-1.06, 2.47, 0.20),
                (1.06, 2.52, 0.29), "CLADDING")
    builder.panel("LowerMouth", [
        (-0.53, 2.522, 0.295), (0.53, 2.522, 0.295),
        (0.45, 2.522, 0.36), (-0.45, 2.522, 0.36),
    ], "BLACK", (0.0, 1.0, 0.0))

    # Clean Kamm tail, inset lamps and a vented deck. No loose wing.
    builder.box("RearBumper", (-1.06, -2.52, 0.22),
                (1.06, -2.47, 0.32), "CLADDING")
    builder.panel("RearBlackPanel", [
        (-0.97, -2.522, 0.40), (0.97, -2.522, 0.40),
        (0.91, -2.522, 0.66), (-0.91, -2.522, 0.66),
    ], "BLACK", (0.0, -1.0, 0.0))
    for side in (-1.0, 1.0):
        for index, (inner, outer, material) in enumerate((
            (0.49, 0.61, "TAIL_RED"),
            (0.64, 0.76, "TAIL_AMBER"),
            (0.79, 0.91, "TAIL_RED"),
        )):
            x0, x1 = sorted((side * inner, side * outer))
            builder.panel(f"TailCell{side}_{index}", [
                (x0, -2.524, 0.445), (x1, -2.524, 0.445),
                (x1, -2.524, 0.615), (x0, -2.524, 0.615),
            ], material, (0.0, -1.0, 0.0))
    for index, y in enumerate((-2.05, -1.86, -1.67, -1.48)):
        builder.box(f"EngineLouver{index}", (-0.58, y, 0.805),
                    (0.58, y + 0.055, 0.832), "BLACK")
    builder.panel("WideTrapezoidExhaust", [
        (-0.27, -2.524, 0.205), (0.27, -2.524, 0.205),
        (0.35, -2.524, 0.29), (-0.35, -2.524, 0.29),
    ], "EXHAUST", (0.0, -1.0, 0.0))

    cut_wheel_wells((chassis, floor, shell))
    body = builder.join()
    assign_zip_uvs(body)
    for name, location in {
        "WHEEL_FL": (-0.98, 1.65, 0.46),
        "WHEEL_FR": (0.98, 1.65, 0.46),
        "WHEEL_RL": (-0.98, -1.60, 0.46),
        "WHEEL_RR": (0.98, -1.60, 0.46),
    }.items():
        empty = bpy.data.objects.new(name, None)
        empty.empty_display_type = "SPHERE"
        empty.empty_display_size = 0.12
        empty.location = location
        bpy.context.collection.objects.link(empty)
    return body


def main() -> None:
    source = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesh", type=Path, required=True)
    parser.add_argument("--blend", type=Path, required=True)
    args = parser.parse_args(source)
    body = build_vehicle()
    args.blend.parent.mkdir(parents=True, exist_ok=True)
    bpy.context.view_layer.objects.active = body
    body.select_set(True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    vertices, triangles = export_emesh(body, args.mesh, "glr_zip")
    print(f"GLR_ZIP_BLENDER body=BODY vertices={vertices} triangles={triangles} "
          f"blend={args.blend} mesh={args.mesh}")


if __name__ == "__main__":
    main()
