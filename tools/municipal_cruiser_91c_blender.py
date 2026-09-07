#!/usr/bin/env python3
"""Blender cook for the original articulated Municipal Cruiser 91C."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from municipal_cruiser_91c_spec import (ATLAS_SIZE, REGIONS, UV_FLIP_U,
                                        UV_PROJECTIONS, WHEELS)
from vesper_vx91_blender import VehicleBuilder, export_emesh


class PatrolBuilder(VehicleBuilder):
    """Keep authored winding on isolated cards while retaining shared helpers."""

    def panel(self, name, vertices, material, desired):
        obj = super().panel(name, vertices, material, desired)
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        for face in bm.faces:
            if face.normal.dot(Vector(desired)) < 0:
                face.normal_flip()
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()
        return obj


def lower_ring(width: float, shoulder: float, top_width: float,
               top: float) -> list[tuple[float, float]]:
    """Ten-sided lower-body section with broad, readable upper planes."""
    return [
        (-width * .62, .18), (-width * .92, .24),
        (-width, .38), (-width, shoulder), (-top_width, top),
        (top_width, top), (width, shoulder), (width, .38),
        (width * .92, .24), (width * .62, .18),
    ]


def beam(builder: PatrolBuilder, name: str, start, end, width: float,
         material: str):
    """Capped triangular frame beam between arbitrary 3D points."""
    direction = (Vector(end) - Vector(start)).normalized()
    reference = Vector((0, 0, 1)) if abs(direction.z) < .9 else Vector((1, 0, 0))
    across = direction.cross(reference).normalized() * width * .5
    depth = direction.cross(across).normalized() * width * .5
    section = (across, -.5 * across + .866 * depth,
               -.5 * across - .866 * depth)
    vertices = [tuple(Vector(point) + offset)
                for point in (start, end) for offset in section]
    return builder.add_mesh(name, vertices,
                            [(0, 2, 1), (3, 4, 5),
                             (0, 1, 4, 3), (1, 2, 5, 4),
                             (2, 0, 3, 5)], material)


def capped_panel(builder: PatrolBuilder, name: str, points, inward,
                 material: str):
    """Thin six-sided panel used for the moving lower door."""
    front = [Vector(point) for point in points]
    back = [point + Vector(inward) for point in front]
    count = len(front)
    vertices = [tuple(point) for point in front + back]
    faces = [tuple(range(count)), tuple(reversed(range(count, count * 2)))]
    faces.extend((i, (i + 1) % count, count + (i + 1) % count, count + i)
                 for i in range(count))
    return builder.add_mesh(name, vertices, faces, material)


def closed_pane(builder: PatrolBuilder, name: str, points, material: str,
                outward, thickness: float = .012):
    """Two-sided capped glass; the rim sits inside its structural frame."""
    desired = Vector(outward).normalized()
    front = [Vector(point) for point in points]
    if (front[1] - front[0]).cross(front[2] - front[0]).dot(desired) < 0:
        front.reverse()
    back = [point - desired * thickness for point in front]
    count = len(front)
    vertices = [tuple(point) for point in front + back]
    faces = [tuple(range(count)), tuple(reversed(range(count, count * 2)))]
    faces.extend((i, (i + 1) % count, count + (i + 1) % count, count + i)
                 for i in range(count))
    return builder.add_mesh(name, vertices, faces, material)


def window_frame(builder: PatrolBuilder, name: str, outer, inner,
                 side: float, material: str):
    """A capped four-sided window ring with a real optical opening."""
    points = [Vector(point) for point in outer + inner]
    points += [point + Vector((-side * .042, 0, 0)) for point in points]
    faces = []
    for index in range(4):
        following = (index + 1) % 4
        faces.extend([
            (index, following, following + 4, index + 4),
            (index + 8, index + 12, following + 12, following + 8),
            (index, index + 8, following + 8, following),
            (index + 4, following + 4, following + 12, index + 12),
        ])
    return builder.add_mesh(name, [tuple(point) for point in points], faces,
                            material)


def arch_lip(builder: PatrolBuilder, name: str, side: float, axle: float):
    """Low-poly fender crown around, never across, a real wheel opening."""
    segments = 7
    vertices = []
    for index in range(segments + 1):
        angle = math.pi * index / segments
        for x, radius in ((side * 1.045, .575), (side * .84, .545),
                          (side * 1.045, .520), (side * .84, .500)):
            vertices.append((x, axle + math.cos(angle) * radius,
                             WHEELS["arch_y"] + math.sin(angle) * radius))
    faces = []
    for index in range(segments):
        a, following = index * 4, (index + 1) * 4
        faces.extend([
            (a, following, following + 1, a + 1),
            (a + 1, following + 1, following + 3, a + 3),
            (a + 3, following + 3, following + 2, a + 2),
            (a + 2, following + 2, following, a),
        ])
    faces.extend([(0, 1, 3, 2),
                  (segments * 4 + 2, segments * 4 + 3,
                   segments * 4 + 1, segments * 4)])
    if side < 0:
        faces = [tuple(reversed(face)) for face in faces]
    material = "SIDE_DRIVER" if side > 0 else "SIDE_PASSENGER"
    return builder.add_mesh(name, vertices, faces, material)


def cut_wheel_wells(shell):
    """Cut four side-only pockets; the central hood/floor/deck stay intact."""
    for label, axle in (("Front", WHEELS["front_z"]),
                        ("Rear", WHEELS["rear_z"])):
        for side, suffix in ((-1., "L"), (1., "R")):
            segments = 12
            x_inner, x_outer = side * .67, side * 1.30
            vertices = []
            for x in sorted((x_inner, x_outer)):
                for index in range(segments):
                    angle = math.tau * index / segments
                    vertices.append((x, axle + math.cos(angle) * .59,
                                     WHEELS["arch_y"] + math.sin(angle) * .53))
            faces = []
            for index in range(segments):
                following = (index + 1) % segments
                faces.append((index, following, segments + following,
                              segments + index))
            faces.extend([tuple(reversed(range(segments))),
                          tuple(segments + i for i in range(segments))])
            mesh = bpy.data.meshes.new(f"{label}{suffix}WellCutterMesh")
            mesh.from_pydata(vertices, [], faces)
            mesh.validate(verbose=False)
            mesh.update()
            cutter = bpy.data.objects.new(f"{label}{suffix}WellCutter", mesh)
            bpy.context.collection.objects.link(cutter)
            modifier = shell.modifiers.new("Cut" + cutter.name, "BOOLEAN")
            modifier.operation = "DIFFERENCE"
            modifier.solver = "EXACT"
            modifier.object = cutter
            bpy.context.view_layer.objects.active = shell
            shell.select_set(True)
            bpy.ops.object.modifier_apply(modifier=modifier.name)
            bpy.data.objects.remove(cutter, do_unlink=True)
    shell.data.update()


def delete_faces(obj, predicate):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.normal_update()
    doomed = [face for face in bm.faces if predicate(face)]
    bmesh.ops.delete(bm, geom=doomed, context="FACES")
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def assign_shell_materials(shell, builder: PatrolBuilder):
    names = ["BODY_TOP", "SIDE_DRIVER", "SIDE_PASSENGER", "BODY_FRONT",
             "BODY_REAR", "BLACK"]
    # Blender's Boolean can leave an empty material slot behind.  The shell is
    # classified from final face geometry below, so rebuild this list cleanly.
    shell.data.materials.clear()
    present = set()
    for name in names:
        if name not in present:
            shell.data.materials.append(builder.material(name))
    material_index = {slot.name: index for index, slot in enumerate(shell.data.materials)}
    for polygon in shell.data.polygons:
        centre = polygon.center
        normal = polygon.normal
        if centre.y > 2.43 and normal.y > .12:
            name = "BODY_FRONT"
        elif centre.y < -2.43 and normal.y < -.12:
            name = "BODY_REAR"
        elif normal.z < -.16:
            name = "BLACK"
        elif abs(normal.x) > .30:
            name = "SIDE_DRIVER" if centre.x > 0 else "SIDE_PASSENGER"
        else:
            name = "BODY_TOP"
        polygon.material_index = material_index[name]
        polygon.use_smooth = False


def circle_panel(builder: PatrolBuilder, name: str, centre, radius: float,
                 material: str):
    cx, cy, cz = centre
    points = [(cx + math.cos(i * math.tau / 8) * radius, cy,
               cz + math.sin(i * math.tau / 8) * radius) for i in range(8)]
    return builder.panel(name, points, material, (0, 1, 0))


def assign_uvs(obj):
    mesh = obj.data
    if mesh.uv_layers:
        mesh.uv_layers.remove(mesh.uv_layers[0])
    uv_layer = mesh.uv_layers.new(name="UVMap")

    def fallback_axes(normal):
        if abs(normal.z) >= max(abs(normal.x), abs(normal.y)):
            return 0, 1
        if abs(normal.y) >= abs(normal.x):
            return 0, 2
        return 1, 2

    for polygon in mesh.polygons:
        material = obj.material_slots[polygon.material_index].material.name
        x0, y0, x1, y1 = REGIONS[material]
        u0, u1 = (x0 + 2) / ATLAS_SIZE, (x1 - 2) / ATLAS_SIZE
        v0, v1 = 1 - (y1 - 2) / ATLAS_SIZE, 1 - (y0 + 2) / ATLAS_SIZE
        points = [mesh.vertices[mesh.loops[i].vertex_index].co
                  for i in polygon.loop_indices]
        if material in UV_PROJECTIONS:
            axes, bounds_pair = UV_PROJECTIONS[material]
            bounds = (bounds_pair[0][0], bounds_pair[0][1],
                      bounds_pair[1][0], bounds_pair[1][1])
        else:
            axes = fallback_axes(polygon.normal)
            bounds = (min(point[axes[0]] for point in points),
                      max(point[axes[0]] for point in points),
                      min(point[axes[1]] for point in points),
                      max(point[axes[1]] for point in points))
        span_a, span_b = bounds[1] - bounds[0], bounds[3] - bounds[2]
        for loop, point in zip(polygon.loop_indices, points):
            a = .5 if span_a < 1e-7 else (point[axes[0]] - bounds[0]) / span_a
            b = .5 if span_b < 1e-7 else (point[axes[1]] - bounds[2]) / span_b
            a, b = max(0., min(1., a)), max(0., min(1., b))
            if material in UV_FLIP_U:
                a = 1 - a
            uv_layer.data[loop].uv = (u0 + a * (u1 - u0),
                                      v0 + b * (v1 - v0))


def join_objects(objects, name):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    result = bpy.context.view_layer.objects.active
    result.name = name
    result.data.name = name + "Mesh"
    return result


def build_vehicle():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    builder = PatrolBuilder()
    body_parts = []
    door_parts = []
    panes = {}

    shell = builder.loft("FacetedLowerBody", [
        (-2.66, lower_ring(.93, .72, .67, .82)),
        (-2.45, lower_ring(1.045, .90, .78, .98)),
        (-2.08, lower_ring(1.05, .94, .82, 1.00)),
        (-1.53, lower_ring(1.05, .95, .83, 1.00)),
        (-1.22, lower_ring(1.04, .95, .82, .99)),
        (-.12, lower_ring(1.01, .94, .80, .98)),
        (.90, lower_ring(1.02, .96, .82, 1.02)),
        (1.08, lower_ring(1.04, .96, .83, 1.03)),
        (1.63, lower_ring(1.05, .94, .82, 1.01)),
        (2.15, lower_ring(1.04, .86, .78, .93)),
        (2.48, lower_ring(1.00, .76, .72, .85)),
        (2.66, lower_ring(.93, .65, .65, .74)),
    ], "BODY_TOP")
    cut_wheel_wells(shell)
    # Open the cabin through the top while retaining shoulder-width sills.
    delete_faces(shell, lambda face:
                 min(v.co.y for v in face.verts) >= -1.221 and
                 max(v.co.y for v in face.verts) <= .901 and
                 abs(face.calc_center_median().x) < .88 and
                 face.normal.z > .18 and min(v.co.z for v in face.verts) > .86)
    # The +X front-door skin is absent from body_open and supplied separately.
    delete_faces(shell, lambda face:
                 min(v.co.y for v in face.verts) >= -.121 and
                 max(v.co.y for v in face.verts) <= .901 and
                 min(v.co.x for v in face.verts) > .78 and
                 face.calc_center_median().z > .50)
    assign_shell_materials(shell, builder)
    body_parts.append(shell)

    body_parts.extend([
        builder.box("CentralChassis", (-.58, -2.47, .18),
                    (.58, 2.48, .34), "BLACK"),
        builder.box("CabinFloor", (-.70, -1.20, .32),
                    (.70, .90, .40), "INTERIOR"),
    ])
    for side in (-1., 1.):
        suffix = "L" if side < 0 else "R"
        body_parts.append(arch_lip(builder, "FrontArch" + suffix, side,
                                   WHEELS["front_z"]))
        body_parts.append(arch_lip(builder, "RearArch" + suffix, side,
                                   WHEELS["rear_z"]))
        lo, hi = sorted((side * .78, side * .995))
        body_parts.append(builder.box("Rocker" + suffix, (lo, -.91, .30),
                                      (hi, 1.02, .54), "BLACK"))

    # Long central hood power bulge: broad and low, not a modern scoop.
    body_parts.append(builder.loft("PowerBulge", [
        (.82, [(-.40, 1.015), (-.30, 1.065), (.30, 1.065), (.40, 1.015)]),
        (1.63, [(-.38, 1.01), (-.28, 1.085), (.28, 1.085), (.38, 1.01)]),
        (2.35, [(-.30, .86), (-.20, .90), (.20, .90), (.30, .86)]),
    ], "BODY_TOP"))

    # Formal roof and fixed structure around six real pane components.
    body_parts.append(builder.loft("RoofCap", [
        (-.80, [(-.72, 1.46), (-.65, 1.53), (.65, 1.53), (.72, 1.46)]),
        (-.30, [(-.74, 1.47), (-.67, 1.55), (.67, 1.55), (.74, 1.47)]),
        (.28, [(-.70, 1.47), (-.64, 1.55), (.64, 1.55), (.70, 1.47)]),
    ], "BODY_TOP"))
    for side in (-1., 1.):
        material = "SIDE_DRIVER" if side > 0 else "SIDE_PASSENGER"
        suffix = "R" if side > 0 else "L"
        body_parts.extend([
            beam(builder, "APillar" + suffix, (side * .94, .90, .99),
                 (side * .70, .28, 1.50), .075, material),
            beam(builder, "RoofRail" + suffix, (side * .70, .28, 1.50),
                 (side * .72, -.80, 1.49), .070, material),
            beam(builder, "BPost" + suffix, (side * .99, -.14, .98),
                 (side * .73, -.16, 1.49), .078, material),
            beam(builder, "CPost" + suffix, (side * .72, -.80, 1.49),
                 (side * .96, -1.23, .99), .105, material),
            beam(builder, "RearWindowSill" + suffix, (side * .99, -.17, .99),
                 (side * .96, -1.23, .99), .060, material),
        ])
        if side < 0:
            body_parts.append(beam(builder, "PassengerFrontSill",
                              (side * .98, .88, .99),
                              (side * .99, -.10, .99), .060, material))

    body_parts.extend([
        beam(builder, "WindshieldHeader", (-.70, .28, 1.50),
             (.70, .28, 1.50), .075, "BODY_TOP"),
        beam(builder, "RearHeader", (.72, -.80, 1.49),
             (-.72, -.80, 1.49), .075, "BODY_TOP"),
    ])

    panes["windshield"] = closed_pane(builder, "Windshield", [
        (-.89, .855, 1.025), (.89, .855, 1.025),
        (.65, .315, 1.475), (-.65, .315, 1.475),
    ], "GLASS_FRONT", (0, 1, .4))
    panes["rear_glass"] = closed_pane(builder, "RearGlass", [
        (.67, -.815, 1.465), (-.67, -.815, 1.465),
        (-.90, -1.19, 1.025), (.90, -1.19, 1.025),
    ], "GLASS_REAR", (0, -1, .4))

    passenger_front = [
        (-.965, .84, 1.015), (-.978, -.08, 1.015),
        (-.742, -.105, 1.455), (-.708, .30, 1.465),
    ]
    driver_front = [(abs(x), y, z) for x, y, z in passenger_front]
    passenger_rear = [
        (-.978, -.19, 1.015), (-.948, -1.13, 1.015),
        (-.735, -.79, 1.445), (-.748, -.205, 1.455),
    ]
    driver_rear = [(abs(x), y, z) for x, y, z in passenger_rear]
    panes["passenger_glass"] = closed_pane(builder, "PassengerGlass",
            passenger_front, "GLASS_SIDE", (-1, 0, 0))
    panes["driver_glass"] = closed_pane(builder, "DriverGlass",
            driver_front, "GLASS_SIDE", (1, 0, 0))
    panes["passenger_rear_glass"] = closed_pane(builder,
            "PassengerRearGlass", passenger_rear, "GLASS_SIDE", (-1, 0, 0))
    panes["driver_rear_glass"] = closed_pane(builder, "DriverRearGlass",
            driver_rear, "GLASS_SIDE", (1, 0, 0))

    # The moving driver door owns its lower skin, capped window ring, mirror,
    # handle and rub strip.  DriverGlass exports separately and shares its pivot.
    driver_outer = [
        (1.006, .90, .54), (1.006, -.12, .54),
        (1.006, -.12, .96), (1.006, .90, .96),
    ]
    door_parts.append(capped_panel(builder, "DriverDoorPanel", driver_outer,
                                   (-.045, 0, 0), "SIDE_DRIVER"))
    door_outer = [(1.002, .88, .985), (1.002, -.12, .985),
                  (.730, -.14, 1.50), (.690, .31, 1.50)]
    door_inner = [(.975, .82, 1.035), (.975, -.065, 1.035),
                  (.755, -.085, 1.445), (.725, .32, 1.445)]
    door_parts.append(window_frame(builder, "DriverDoorWindowFrame",
                                   door_outer, door_inner, 1., "SIDE_DRIVER"))
    door_parts.extend([
        builder.box("DriverMirror", (.962, .52, 1.045),
                    (1.04, .70, 1.155), "BLACK"),
        builder.panel("DriverMirrorGlass", [(1.041, .54, 1.065),
                      (1.041, .68, 1.065), (1.041, .68, 1.135),
                      (1.041, .54, 1.135)], "LENS_CLEAR", (1, 0, 0)),
        builder.panel("DriverHandle", [(1.027, -.015, .91),
                      (1.027, .20, .91), (1.027, .20, .955),
                      (1.027, -.015, .955)], "METAL", (1, 0, 0)),
        builder.panel("DriverRubStrip", [(1.025, -.08, .62),
                      (1.025, .82, .62), (1.025, .82, .68),
                      (1.025, -.08, .68)], "BLACK", (1, 0, 0)),
    ])

    # Fixed passenger/rear door hardware gives Car 5-like readable seams.
    body_parts.extend([
        builder.box("PassengerMirror", (-1.04, .52, 1.045),
                    (-.962, .70, 1.155), "BLACK"),
        builder.panel("PassengerMirrorGlass", [(-1.041, .68, 1.065),
                      (-1.041, .54, 1.065), (-1.041, .54, 1.135),
                      (-1.041, .68, 1.135)], "LENS_CLEAR", (-1, 0, 0)),
    ])
    for side in (-1., 1.):
        material = "SIDE_DRIVER" if side > 0 else "SIDE_PASSENGER"
        suffix = "R" if side > 0 else "L"
        x = side * 1.027
        if side < 0:
            body_parts.append(builder.panel("FrontHandle" + suffix,
                [(x, -.015, .91), (x, .20, .91), (x, .20, .955),
                 (x, -.015, .955)], "METAL", (side, 0, 0)))
            body_parts.append(builder.panel("FrontRubStrip" + suffix,
                [(x, -.08, .62), (x, .82, .62), (x, .82, .68),
                 (x, -.08, .68)], "BLACK", (side, 0, 0)))
        body_parts.extend([
            builder.panel("RearHandle" + suffix,
                [(x, -.95, .91), (x, -.72, .91), (x, -.72, .955),
                 (x, -.95, .955)], "METAL", (side, 0, 0)),
            builder.panel("RearRubStrip" + suffix,
                [(x, -.91, .62), (x, -.20, .62), (x, -.20, .68),
                 (x, -.91, .68)], "BLACK", (side, 0, 0)),
        ])

    # Hollow cabin contents: floor, dash, four seating positions and controls.
    body_parts.extend([
        builder.box("Dashboard", (-.82, .69, .91),
                    (.82, .86, 1.08), "INTERIOR"),
        builder.box("RearBench", (-.72, -1.02, .48),
                    (.72, -.48, .69), "SEAT"),
        builder.box("RearSeatBack", (-.72, -1.12, .67),
                    (.72, -1.00, 1.16), "SEAT"),
    ])
    for side in (-1., 1.):
        centre = side * .42
        body_parts.extend([
            builder.box("FrontSeatBase" + str(side),
                        (centre - .25, -.10, .43),
                        (centre + .25, .44, .58), "INTERIOR"),
            builder.box("FrontSeatCushion" + str(side),
                        (centre - .24, -.08, .58),
                        (centre + .24, .40, .72), "SEAT"),
            builder.box("FrontSeatBack" + str(side),
                        (centre - .24, -.25, .69),
                        (centre + .24, -.17, 1.18), "SEAT"),
        ])
    steering_vertices = []
    for depth in (.535, .565):
        for radius in (.145, .105):
            steering_vertices += [
                (.42 + radius * math.cos(i * math.tau / 8), depth,
                 1.04 + radius * math.sin(i * math.tau / 8)) for i in range(8)
            ]
    steering_faces = []
    for i in range(8):
        following = (i + 1) % 8
        steering_faces.extend([
            (i, following, following + 8, i + 8),
            (i + 16, i + 24, following + 24, following + 16),
            (i, i + 16, following + 16, following),
            (i + 8, following + 8, following + 24, i + 24),
        ])
    body_parts.extend([
        builder.add_mesh("SteeringRim", steering_vertices, steering_faces,
                         "BLACK"),
        builder.box("SteeringSpoke", (.29, .545, 1.025),
                    (.55, .558, 1.055), "METAL"),
        beam(builder, "SteeringColumn", (.42, .55, 1.04),
             (.42, .74, .94), .045, "BLACK"),
        builder.box("BrakePedal", (.29, .69, .405),
                    (.37, .79, .435), "METAL"),
        builder.box("ThrottlePedal", (.49, .70, .405),
                    (.56, .80, .435), "METAL"),
    ])

    # Four inset sealed beams, period rear lenses, bumpers and push hardware.
    body_parts.append(builder.panel("FrontLampPocket", [(-.96, 2.667, .54),
                      (.96, 2.667, .54), (.96, 2.667, .78),
                      (-.96, 2.667, .78)], "BLACK", (0, 1, 0)))
    for index, (x0, x1) in enumerate(((-.91, -.50), (-.46, -.08),
                                      (.08, .46), (.50, .91))):
        body_parts.append(builder.panel("Headlight" + str(index), [
            (x0, 2.669, .58), (x1, 2.669, .58),
            (x1, 2.669, .74), (x0, 2.669, .74),
        ], "HEADLIGHT", (0, 1, 0)))
    body_parts.extend([
        builder.panel("FrontGrille", [(-.58, 2.670, .38),
                      (.58, 2.670, .38), (.58, 2.670, .53),
                      (-.58, 2.670, .53)], "BLACK", (0, 1, 0)),
        builder.box("FrontBumper", (-1.05, 2.62, .30),
                    (1.05, 2.68, .41), "BLACK"),
        builder.box("PushTop", (-.82, 2.675, .68),
                    (.82, 2.71, .74), "BLACK"),
        builder.box("PushLower", (-.87, 2.675, .40),
                    (.87, 2.71, .47), "BLACK"),
        builder.box("PushPostL", (-.68, 2.675, .35),
                    (-.58, 2.71, .78), "BLACK"),
        builder.box("PushPostR", (.58, 2.675, .35),
                    (.68, 2.71, .78), "BLACK"),
        builder.box("RearBumper", (-1.05, -2.71, .30),
                    (1.05, -2.62, .43), "BLACK"),
    ])
    for side in (-1., 1.):
        x0, x1 = sorted((side * .91, side * .53))
        body_parts.append(builder.panel("BrakeLamp" + str(side), [
            (x0, -2.669, .58), (x1, -2.669, .58),
            (x1, -2.669, .75), (x0, -2.669, .75),
        ], "TAIL_RED", (0, -1, 0)))
        ax0, ax1 = sorted((side * .50, side * .31))
        body_parts.append(builder.panel("RearAmber" + str(side), [
            (ax0, -2.670, .58), (ax1, -2.670, .58),
            (ax1, -2.670, .75), (ax0, -2.670, .75),
        ], "TAIL_AMBER", (0, -1, 0)))
    body_parts.extend([
        builder.panel("RearPlate", [(-.24, -2.671, .46),
                      (.24, -2.671, .46), (.24, -2.671, .59),
                      (-.24, -2.671, .59)], "LENS_CLEAR", (0, -1, 0)),
        builder.box("LightbarBase", (-.76, -.37, 1.55),
                    (.76, -.12, 1.61), "BLACK"),
        builder.box("LightbarRed", (-.70, -.35, 1.61),
                    (-.05, -.14, 1.77), "LIGHTBAR_RED"),
        builder.box("LightbarCentre", (-.05, -.35, 1.61),
                    (.05, -.14, 1.77), "METAL"),
        builder.box("LightbarBlue", (.05, -.35, 1.61),
                    (.70, -.14, 1.77), "LIGHTBAR_BLUE"),
        beam(builder, "WhipAntenna", (-.78, -2.06, 1.00),
             (-.76, -2.03, 1.58), .022, "BLACK"),
        beam(builder, "SpotlightArm", (.88, .63, 1.09),
             (.94, .66, 1.18), .038, "BLACK"),
        builder.cylinder("SpotlightHousing", (.94, .655, 1.18),
                         .085, .085, 8, "BLACK"),
        circle_panel(builder, "SpotlightLens", (.94, .699, 1.18),
                     .070, "LENS_CLEAR"),
    ])

    all_meshes = body_parts + door_parts + list(panes.values())
    # Remove only exact duplicate/degenerate fragments created by Boolean seams.
    for obj in all_meshes:
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
        bmesh.ops.dissolve_degenerate(bm, edges=list(bm.edges), dist=1e-7)
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()
        assign_uvs(obj)

    body_open = join_objects(body_parts, "BODY_OPEN")
    driver_door = join_objects(door_parts, "DRIVER_DOOR")
    return body_open, driver_door, panes, builder


def configure_blend_materials(builder, texture_path: Path):
    atlas = bpy.data.images.load(str(texture_path), check_existing=True)
    for material in builder.materials.values():
        material.use_nodes = True
        shader = material.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Roughness"].default_value = 1.0
        if material.name.startswith("GLASS_"):
            shader.inputs["Alpha"].default_value = .58
            material.diffuse_color = (.10, .22, .29, .58)
        texture = material.node_tree.nodes.new("ShaderNodeTexImage")
        texture.image = atlas
        texture.interpolation = "Closest"
        material.node_tree.links.new(texture.outputs["Color"],
                                     shader.inputs["Base Color"])
        if "Alpha" in texture.outputs:
            material.node_tree.links.new(texture.outputs["Alpha"],
                                         shader.inputs["Alpha"])


def duplicate_object(obj, name):
    clone = obj.copy()
    clone.data = obj.data.copy()
    clone.name = name
    bpy.context.collection.objects.link(clone)
    return clone


def main():
    args_source = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesh", type=Path, required=True)
    parser.add_argument("--blend", type=Path, required=True)
    args = parser.parse_args(args_source)
    body_open, door, panes, builder = build_vehicle()

    for name, location in {
        "WHEEL_FL": (-WHEELS["x"], WHEELS["front_z"], WHEELS["arch_y"]),
        "WHEEL_FR": (WHEELS["x"], WHEELS["front_z"], WHEELS["arch_y"]),
        "WHEEL_RL": (-WHEELS["x"], WHEELS["rear_z"], WHEELS["arch_y"]),
        "WHEEL_RR": (WHEELS["x"], WHEELS["rear_z"], WHEELS["arch_y"]),
    }.items():
        # The tuple is assembled directly in Blender X/forward/up order.
        empty = bpy.data.objects.new(name, None)
        empty.empty_display_type = "SPHERE"
        empty.empty_display_size = .10
        empty.location = (location[0], location[1], location[2])
        bpy.context.collection.objects.link(empty)

    texture_path = (Path(__file__).resolve().parents[1] /
                    "assets/textures/vehicles/municipal_cruiser_91c/body.png")
    configure_blend_materials(builder, texture_path)
    args.blend.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))

    print("CRUISER_91C_OPEN", export_emesh(
        body_open, args.mesh.with_name("body_open.emesh"),
        "municipal_cruiser_91c"))
    print("CRUISER_91C_DOOR", export_emesh(
        door, args.mesh.with_name("driver_door.emesh"),
        "municipal_cruiser_91c"))
    for filename, pane in panes.items():
        print("CRUISER_91C_GLASS", filename, export_emesh(
            pane, args.mesh.with_name(filename + ".emesh"),
            "municipal_cruiser_91c"))

    closed_parts = [duplicate_object(body_open, "ClosedBodyPart"),
                    duplicate_object(door, "ClosedDoorPart")]
    closed_parts.extend(duplicate_object(pane, "Closed" + name)
                        for name, pane in panes.items())
    closed = join_objects(closed_parts, "BODY")
    print("CRUISER_91C_CLOSED", export_emesh(
        closed, args.mesh, "municipal_cruiser_91c"))


if __name__ == "__main__":
    main()
