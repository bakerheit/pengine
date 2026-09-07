#!/usr/bin/env python3
"""Deterministic low-poly 1991 Metro police sedan with a working door."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from municipal_cruiser_91d_spec import (  # noqa: E402
    ATLAS_SIZE,
    DOOR,
    PANE_FILES,
    REGIONS,
    SHAPE,
    UV_PROJECTIONS,
    WHEELS,
)
from vesper_vx91_blender import VehicleBuilder, export_emesh  # noqa: E402


class MetroBuilder(VehicleBuilder):
    def panel(self, name, vertices, material, desired):
        obj = super().panel(name, vertices, material, desired)
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        target = Vector(desired)
        for face in bm.faces:
            if face.normal.dot(target) < 0:
                face.normal_flip()
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()
        return obj


def profile(forward: float) -> tuple[float, float]:
    """Half-width and shoulder height at a longitudinal station."""
    keys = (
        (-2.540, .91, .84), (-2.43, .97, .97), (-2.00, 1.005, 1.05),
        (-1.43, 1.010, 1.07), (-.92, 1.005, 1.06),
        (.72, 1.005, 1.07), (.97, 1.010, 1.07),
        (1.48, 1.010, 1.08), (1.96, 1.005, 1.06),
        (2.40, .98, .99), (2.540, .91, .86),
    )
    for left, right in zip(keys, keys[1:]):
        if left[0] - 1e-7 <= forward <= right[0] + 1e-7:
            t = (forward - left[0]) / (right[0] - left[0])
            return (left[1] + t * (right[1] - left[1]),
                    left[2] + t * (right[2] - left[2]))
    raise ValueError(forward)


def body_stations() -> list[tuple[float, float]]:
    """Station feet rise around four true outer-side wheel openings."""
    radius = SHAPE["arch_long_radius"]
    height = SHAPE["arch_height_radius"]
    centre = WHEELS["arch_y"]
    result = [(-2.540, .20), (-2.28, .20)]
    for axle in (WHEELS["rear_z"], WHEELS["front_z"]):
        result.extend(((axle - radius, .20), (axle - radius, centre)))
        result.extend(
            (axle - radius * math.cos(index * math.pi / 6),
             centre + height * math.sin(index * math.pi / 6))
            for index in range(1, 6)
        )
        result.extend(((axle + radius, centre), (axle + radius, .20)))
        if axle < 0:
            result.extend(((-.68, .20), (DOOR["rear_z"], .20),
                           (DOOR["front_z"], .20)))
    result.extend(((2.34, .20), (2.540, .20)))
    return sorted(result, key=lambda item: item[0])


def add_material(obj, builder: MetroBuilder, name: str) -> int:
    if obj.data.materials.find(name) < 0:
        obj.data.materials.append(builder.material(name))
    return obj.data.materials.find(name)


def build_side_shell(builder: MetroBuilder, side: float):
    rings = []
    for forward, low in body_stations():
        width, top = profile(forward)
        # Inboard rail, outer lower lip, side skin, bevelled shoulder, inner top.
        points = ((.62, low), (width, low + .035),
                  (width, top - .10), (.925 * width, top), (.67, top))
        rings.append((forward, [(side * x, z) for x, z in points]))
    shell = builder.loft("MetroSideShell" + ("L" if side < 0 else "R"),
                         rings, "BODY_GREEN")
    top_index = add_material(shell, builder, "BODY_TOP")
    shadow_index = add_material(shell, builder, "SHADOW")
    for face in shell.data.polygons:
        if face.normal.z > .60:
            face.material_index = top_index
        elif face.normal.z < -.18:
            face.material_index = shadow_index
    if side > 0:
        bm = bmesh.new()
        bm.from_mesh(shell.data)
        doorway = []
        for face in bm.faces:
            centre = face.calc_center_median()
            if (DOOR["rear_z"] + 1e-5 < centre.y < DOOR["front_z"] - 1e-5
                    and centre.z > DOOR["sill_y"] - .015
                    and centre.x > .58):
                doorway.append(face)
        bmesh.ops.delete(bm, geom=doorway, context="FACES")
        bm.to_mesh(shell.data)
        bm.free()
        shell.data.update()
    return shell


def beam(builder: MetroBuilder, name: str, start, end, width: float,
         material: str):
    direction = (Vector(end) - Vector(start)).normalized()
    reference = Vector((0, 0, 1)) if abs(direction.z) < .88 else Vector((0, 1, 0))
    across = direction.cross(reference).normalized() * width * .5
    depth = direction.cross(across).normalized() * width * .5
    section = (across + depth, across - depth,
               -across - depth, -across + depth)
    vertices = [tuple(Vector(point) + offset)
                for point in (start, end) for offset in section]
    faces = ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))
    return builder.add_mesh(name, vertices, list(faces), material)


def closed_pane(builder: MetroBuilder, name: str, points, outward,
                thickness: float = .010):
    desired = Vector(outward).normalized()
    front = [Vector(point) for point in points]
    if (front[1] - front[0]).cross(front[2] - front[0]).dot(desired) < 0:
        front.reverse()
    back = [point - desired * thickness for point in front]
    count = len(front)
    vertices = [tuple(point) for point in front + back]
    faces = [tuple(range(count)), tuple(reversed(range(count, count * 2)))]
    faces.extend((index, (index + 1) % count,
                  count + (index + 1) % count, count + index)
                 for index in range(count))
    return builder.add_mesh(name, vertices, faces, "GLASS")


def side_x(side: float, height: float) -> float:
    return side * (0.981 - (height - 1.06) * (.225 / .58))


def side_card(builder: MetroBuilder, name: str, side: float, yz,
              material: str, x: float = 1.009):
    return builder.panel(name, [(side * x, forward, height)
                                for forward, height in yz],
                         material, (side, 0, 0))


def faceted_bumper(builder: MetroBuilder, name: str, front: bool):
    y0, y1 = ((2.46, 2.540) if front else (-2.540, -2.46))
    ring = [(-.87, .28), (-1.01, .35), (-1.01, .48), (-.91, .54),
            (.91, .54), (1.01, .48), (1.01, .35), (.87, .28)]
    return builder.loft(name, [(y0, ring), (y1, ring)], "METAL")


def steering_rim(builder: MetroBuilder):
    vertices = []
    centre = Vector((.43, .55, 1.13))
    for depth in (-.014, .014):
        for radius in (.15, .108):
            for index in range(8):
                angle = math.tau * index / 8
                vertices.append(tuple(centre + Vector((radius * math.cos(angle),
                                                       depth,
                                                       radius * math.sin(angle)))))
    faces = []
    for index in range(8):
        nxt = (index + 1) % 8
        faces.extend(((index, nxt, 8 + nxt, 8 + index),
                      (16 + index, 24 + index, 24 + nxt, 16 + nxt),
                      (index, 16 + index, 16 + nxt, nxt),
                      (8 + index, 8 + nxt, 24 + nxt, 24 + index)))
    return builder.add_mesh("SteeringRim", vertices, faces, "RUBBER")


def add_interior(builder: MetroBuilder):
    parts = [
        builder.box("CabinFloor", (-.74, -1.17, .37), (.74, .69, .43),
                    "INTERIOR"),
        builder.box("TransmissionTunnel", (-.13, -1.08, .43),
                    (.13, .58, .55), "INTERIOR"),
        builder.box("Firewall", (-.75, .68, .43), (.75, .74, 1.06),
                    "SHADOW"),
        builder.box("RearBulkhead", (-.75, -1.18, .43),
                    (.75, -1.12, 1.06), "SHADOW"),
    ]
    for row, forward in (("Front", .06), ("Rear", -.75)):
        for suffix, x in (("L", -.43), ("R", .43)):
            parts.append(builder.box(row + "SeatCushion" + suffix,
                                     (x - .23, forward - .24, .58),
                                     (x + .23, forward + .22, .76),
                                     "INTERIOR"))
            parts.append(builder.box(row + "SeatBack" + suffix,
                                     (x - .23, forward - .28, .73),
                                     (x + .23, forward - .20, 1.29),
                                     "INTERIOR"))
    parts.append(builder.loft("Dashboard", [
        (.48, [(-.73, .94), (-.73, 1.18), (.73, 1.18), (.73, .94)]),
        (.68, [(-.73, .91), (-.73, 1.07), (.73, 1.07), (.73, .91)]),
    ], "INTERIOR"))
    parts.extend((
        builder.box("GaugeBinnacle", (.18, .49, 1.12),
                    (.67, .64, 1.25), "BLACK"),
        steering_rim(builder),
        builder.box("SteeringColumn", (.40, .55, 1.07),
                    (.46, .70, 1.13), "METAL"),
        builder.box("RadioConsole", (-.15, .50, .74),
                    (.15, .67, 1.01), "BLACK"),
    ))
    for x in (.33, .53):
        parts.append(builder.box("Pedal" + str(x), (x - .035, .58, .435),
                                 (x + .035, .67, .47), "METAL"))
    parts.append(builder.panel("RearViewMirror", [(-.16, .26, 1.48),
                                                    (.16, .26, 1.48),
                                                    (.16, .26, 1.57),
                                                    (-.16, .26, 1.57)],
                               "BLACK", (0, 1, 0)))
    return parts


def add_greenhouse(builder: MetroBuilder, door_parts: list):
    body_parts = []
    roof_ring = [(-.75, 1.63), (-.69, 1.70), (.69, 1.70), (.75, 1.63)]
    body_parts.append(builder.loft("TallMetroRoof", [(-.89, roof_ring),
                                                       (.27, roof_ring)],
                                   "BODY_TOP"))
    for side in (-1., 1.):
        suffix = "L" if side < 0 else "R"
        a_low = (side * .977, .72, 1.06)
        a_top = (side * .755, .27, 1.66)
        b_low = (side * .977, -.32, 1.06)
        b_top = (side * .755, -.32, 1.66)
        c_low = (side * .977, -1.18, 1.06)
        c_top = (side * .755, -.89, 1.66)
        for label, start, end in (
            ("APillar", a_low, a_top), ("BPillar", b_low, b_top),
            ("CPillar", c_low, c_top), ("RoofRailFront", a_top, b_top),
            ("RoofRailRear", b_top, c_top),
            ("WindowSillFront", a_low, b_low),
            ("WindowSillRear", b_low, c_low),
        ):
            body_parts.append(beam(builder, "Metro" + label + suffix,
                                   start, end, .066,
                                   "BODY_TOP" if "RoofRail" in label
                                   else "BODY_GREEN"))
    for label, start, end in (
        ("WindshieldHeader", (-.755, .27, 1.66), (.755, .27, 1.66)),
        ("WindshieldCowl", (-.977, .72, 1.06), (.977, .72, 1.06)),
        ("RearHeader", (-.755, -.89, 1.66), (.755, -.89, 1.66)),
        ("RearShelfRail", (-.977, -1.18, 1.06), (.977, -1.18, 1.06)),
    ):
        body_parts.append(beam(builder, label, start, end, .068, "BODY_GREEN"))

    # The opening front door owns its shell, frame, mark, handle, and mirror.
    door_parts.append(builder.box("DriverDoorShell", (.94, -.32, .49),
                                  (1.01, .72, 1.06), "DOOR_CREAM"))
    driver_edges = (
        ((.972, .68, 1.07), (.758, .24, 1.65)),
        ((.972, -.28, 1.07), (.758, -.28, 1.65)),
        ((.972, .68, 1.07), (.972, -.28, 1.07)),
        ((.758, .24, 1.65), (.758, -.28, 1.65)),
    )
    for index, (start, end) in enumerate(driver_edges):
        door_parts.append(beam(builder, "DriverDoorFrame" + str(index),
                               start, end, .052, "DOOR_CREAM"))
    door_parts.append(side_card(builder, "DriverPoliceMark", 1.,
                                [(-.28, .55), (.68, .55), (.68, 1.03),
                                 (-.28, 1.03)], "POLICE", 1.014))
    door_parts.append(builder.box("DriverHandle", (.994, -.12, .96),
                                  (1.01, .08, 1.02), "METAL"))
    door_parts.append(builder.box("DriverMirror", (.965, .46, 1.07),
                                  (1.01, .66, 1.20), "RUBBER"))
    return body_parts


def add_static_doors(builder: MetroBuilder):
    parts = []
    for side in (-1., 1.):
        suffix = "L" if side < 0 else "R"
        if side < 0:
            parts.append(side_card(builder, "PassengerFrontDoorShell", side,
                                   [(-.32, .49), (.72, .49), (.72, 1.06),
                                    (-.32, 1.06)], "DOOR_CREAM"))
            parts.append(side_card(builder, "PassengerPoliceMark", side,
                                   [(-.28, .55), (.68, .55), (.68, 1.03),
                                    (-.28, 1.03)], "POLICE", 1.014))
            parts.append(builder.box("PassengerMirror",
                                     (-1.01, .46, 1.07),
                                     (-.965, .66, 1.20), "RUBBER"))
        parts.append(side_card(builder, "RearDoorShell" + suffix, side,
                               [(-1.18, .49), (-.32, .49), (-.32, 1.06),
                                (-1.18, 1.06)], "DOOR_CREAM"))
        parts.append(side_card(builder, "RearHandle" + suffix, side,
                               [(-1.08, .96), (-.88, .96),
                                (-.88, 1.02), (-1.08, 1.02)], "METAL"))
        if side < 0:
            parts.append(side_card(builder, "FrontHandle" + suffix, side,
                                   [(-.12, .96), (.08, .96),
                                    (.08, 1.02), (-.12, 1.02)], "METAL"))
        parts.append(side_card(builder, "RearDoorSeam" + suffix, side,
                               [(-.34, .49), (-.32, .49),
                                (-.32, 1.05), (-.34, 1.05)], "SEAM"))
        if side < 0:
            parts.append(side_card(builder, "FrontDoorSeam" + suffix, side,
                                   [(.70, .49), (.72, .49),
                                    (.72, 1.05), (.70, 1.05)], "SEAM"))
    return parts


def add_panes(builder: MetroBuilder):
    panes = {
        "windshield": closed_pane(builder, "windshield", [
            (-.89, .695, 1.105), (.89, .695, 1.105),
            (.705, .295, 1.625), (-.705, .295, 1.625),
        ], (0, 1, .5)),
        "rear_glass": closed_pane(builder, "rear_glass", [
            (.705, -.905, 1.625), (-.705, -.905, 1.625),
            (-.89, -1.155, 1.105), (.89, -1.155, 1.105),
        ], (0, -1, .45)),
    }
    front_yz = ((.655, 1.105), (-.275, 1.105),
                (-.275, 1.615), (.255, 1.615))
    rear_yz = ((-.375, 1.105), (-1.135, 1.105),
               (-.875, 1.615), (-.375, 1.615))
    for side, front_name, rear_name in (
        (-1., "passenger_glass", "passenger_rear_glass"),
        (1., "driver_glass", "driver_rear_glass"),
    ):
        panes[front_name] = closed_pane(
            builder, front_name,
            [(side_x(side, height), forward, height)
             for forward, height in front_yz], (side, 0, 0))
        panes[rear_name] = closed_pane(
            builder, rear_name,
            [(side_x(side, height), forward, height)
             for forward, height in rear_yz], (side, 0, 0))
    return panes


def add_exterior_details(builder: MetroBuilder):
    parts = [faceted_bumper(builder, "FrontFacetedBumper", True),
             faceted_bumper(builder, "RearFacetedBumper", False),
             builder.box("FrontReceiver", (-.94, 2.495, .48),
                         (.94, 2.540, .97), "FRONT"),
             builder.box("RearReceiver", (-.94, -2.540, .48),
                         (.94, -2.495, .97), "REAR")]
    for side in (-1., 1.):
        suffix = "L" if side < 0 else "R"
        for index, (inner, outer) in enumerate(((.46, .68), (.71, .93))):
            x0, x1 = sorted((side * inner, side * outer))
            parts.append(builder.panel("PairedSealedBeam" + suffix + str(index),
                                       [(x0, 2.540, .70),
                                        (x1, 2.540, .70),
                                        (x1, 2.540, .88),
                                        (x0, 2.540, .88)],
                                       "HEADLIGHT", (0, 1, 0)))
        x0, x1 = sorted((side * .74, side * .94))
        parts.append(builder.panel("FrontIndicator" + suffix,
                                   [(x0, 2.540, .55), (x1, 2.540, .55),
                                    (x1, 2.540, .66), (x0, 2.540, .66)],
                                   "AMBER", (0, 1, 0)))
        x0, x1 = sorted((side * .56, side * .91))
        parts.append(builder.panel("RearTail" + suffix,
                                   [(x1, -2.540, .70), (x0, -2.540, .70),
                                    (x0, -2.540, .91), (x1, -2.540, .91)],
                                   "TAIL_RED", (0, -1, 0)))
        x0, x1 = sorted((side * .38, side * .54))
        parts.append(builder.panel("RearReverse" + suffix,
                                   [(x1, -2.540, .70), (x0, -2.540, .70),
                                    (x0, -2.540, .91), (x1, -2.540, .91)],
                                   "REVERSE", (0, -1, 0)))
        for strip_index, (start, end) in enumerate((
            (-2.34, -1.96), (-.90, .95), (2.01, 2.34),
        )):
            parts.append(side_card(builder,
                                   "RubStrip" + suffix + str(strip_index), side,
                                   [(start, .61), (end, .61),
                                    (end, .67), (start, .67)], "RUBBER"))

    # Narrow slatted grille gives this attempt its own upright city-car face.
    for x in (-.34, -.23, -.12, 0., .12, .23, .34):
        parts.append(builder.panel("GrilleBar" + str(x),
                                   [(x - .014, 2.540, .56),
                                    (x + .014, 2.540, .56),
                                    (x + .014, 2.540, .91),
                                    (x - .014, 2.540, .91)],
                                   "METAL", (0, 1, 0)))
    for height in (.57, .88):
        parts.append(builder.panel("GrilleRail" + str(height),
                                   [(-.39, 2.540, height),
                                    (.39, 2.540, height),
                                    (.39, 2.540, height + .03),
                                    (-.39, 2.540, height + .03)],
                                   "METAL", (0, 1, 0)))

    for x in (-.34, .34):
        parts.append(builder.box("PushUpright" + str(x),
                                 (x - .035, 2.505, .38),
                                 (x + .035, 2.540, .90), "RUBBER"))
    for height in (.54, .80):
        parts.append(builder.box("PushCross" + str(height),
                                 (-.54, 2.510, height - .035),
                                 (.54, 2.540, height + .035), "RUBBER"))

    # A full-width lightbar, one driver spotlight, and a rear whip antenna.
    for x in (-.43, .43):
        parts.append(builder.box("LightbarFoot" + str(x),
                                 (x - .04, -.16, 1.70),
                                 (x + .04, -.02, 1.74), "RUBBER"))
    parts.extend((
        builder.box("LightbarBase", (-.76, -.22, 1.70),
                    (.76, .12, 1.74), "RUBBER"),
        builder.box("LightbarRed", (-.72, -.20, 1.74),
                    (-.06, .10, 1.88), "LIGHTBAR_RED"),
        builder.box("LightbarBlue", (.06, -.20, 1.74),
                    (.72, .10, 1.88), "LIGHTBAR_BLUE"),
        builder.box("LightbarSpeaker", (-.052, -.19, 1.74),
                    (.052, .09, 1.85), "BLACK"),
        builder.cylinder("DriverSpotlight", (.90, .49, 1.45),
                         .10, .075, 8, "METAL"),
        builder.cylinder("DriverSpotlightLens", (.90, .532, 1.45),
                         .078, .014, 8, "HEADLIGHT"),
        beam(builder, "DriverSpotlightArm", (.87, .43, 1.34),
             (.90, .48, 1.42), .035, "METAL"),
        beam(builder, "RearWhipAntenna", (.58, -1.78, 1.05),
             (.61, -1.92, 1.62), .018, "BLACK"),
        builder.box("RearPlateRecess", (-.30, -2.540, .52),
                    (.30, -2.505, .66), "BLACK"),
    ))
    return parts


def map_uvs(obj):
    mesh = obj.data
    uv = mesh.uv_layers.new(name="UVMap")
    for face in mesh.polygons:
        material = mesh.materials[face.material_index].name
        x0, y0, x1, y1 = REGIONS[material]
        points = [mesh.vertices[mesh.loops[index].vertex_index].co
                  for index in face.loop_indices]
        normal = face.normal
        if material in UV_PROJECTIONS:
            axes, bounds = UV_PROJECTIONS[material]
        else:
            if abs(normal.z) >= max(abs(normal.x), abs(normal.y)):
                axes = (0, 1)
            elif abs(normal.y) > abs(normal.x):
                axes = (0, 2)
            else:
                axes = (1, 2)
            bounds = tuple((min(point[axis] for point in points),
                            max(point[axis] for point in points))
                           for axis in axes)
        for loop, point in zip(face.loop_indices, points):
            values = []
            for axis, (low, high) in zip(axes, bounds):
                values.append(.5 if high - low < 1e-7 else
                              max(0., min(1., (point[axis] - low) /
                                              (high - low))))
            if material == "POLICE" and normal.x > 0:
                values[0] = 1 - values[0]
            if material == "REAR":
                values[0] = 1 - values[0]
            uv.data[loop].uv = (
                (x0 + 2 + values[0] * (x1 - x0 - 4)) / ATLAS_SIZE,
                1 - (y1 - 2 - values[1] * (y1 - y0 - 4)) / ATLAS_SIZE,
            )


def clean_object(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
    bmesh.ops.dissolve_degenerate(bm, edges=list(bm.edges), dist=1e-7)
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()
    map_uvs(obj)


def join_objects(objects, name: str, data_name: str):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    joined = bpy.context.view_layer.objects.active
    joined.name = name
    joined.data.name = data_name
    return joined


def add_wheel_anchors():
    for side, suffix in ((-1., "L"), (1., "R")):
        for axle, label in ((WHEELS["front_z"], "F"),
                            (WHEELS["rear_z"], "R")):
            empty = bpy.data.objects.new("WHEEL_" + label + suffix, None)
            empty.empty_display_type = "SPHERE"
            empty.empty_display_size = .10
            empty.location = (side * WHEELS["x"], axle, WHEELS["arch_y"])
            bpy.context.collection.objects.link(empty)


def build_vehicle(articulated: bool = False):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for material in list(bpy.data.materials):
        bpy.data.materials.remove(material)
    builder = MetroBuilder()
    door_parts = []
    body_parts = [build_side_shell(builder, -1.), build_side_shell(builder, 1.)]
    body_parts.append(builder.box("CentralChassis", (-.46, -2.48, .20),
                                  (.46, 2.48, .38), "SHADOW"))
    for name, low, high in (("BroadMetroHood", .72, 2.540),
                            ("FormalMetroDeck", -2.540, -1.18)):
        stations = [low, high]
        stations.extend(axle for axle in (WHEELS["rear_z"], WHEELS["front_z"])
                        if low < axle < high)
        sections = []
        for forward in sorted(set(stations)):
            top = profile(forward)[1]
            sections.append((forward, [(-.67, top - .035), (-.67, top),
                                       (.67, top), (.67, top - .035)]))
        body_parts.append(builder.loft(name, sections, "BODY_TOP"))
    body_parts.extend(add_interior(builder))
    body_parts.extend(add_greenhouse(builder, door_parts))
    body_parts.extend(add_static_doors(builder))
    panes = add_panes(builder)
    body_parts.extend(add_exterior_details(builder))

    pane_objects = set(panes.values())
    door_objects = set(door_parts)
    body_objects = [obj for obj in builder.objects
                    if obj not in pane_objects and obj not in door_objects]
    for obj in builder.objects:
        clean_object(obj)
    add_wheel_anchors()
    if not articulated:
        closed = join_objects(builder.objects, "BODY", "MunicipalCruiser91DMetroBody")
        assert len([obj for obj in bpy.context.scene.objects
                    if obj.type == "MESH"]) == 1
        return closed
    body_open = join_objects(body_objects, "BODY_OPEN",
                             "MunicipalCruiser91DMetroOpenBody")
    driver_door = join_objects(door_parts, "DRIVER_DOOR",
                               "MunicipalCruiser91DMetroDriverDoor")
    assert set(panes) == set(PANE_FILES)
    assert len([obj for obj in bpy.context.scene.objects
                if obj.type == "MESH"]) == 8
    return body_open, driver_door, panes


def configure_materials(objects, texture_path: Path):
    atlas = bpy.data.images.load(str(texture_path), check_existing=True)
    for obj in objects:
        for slot in obj.material_slots:
            material = slot.material
            material.use_nodes = True
            shader = material.node_tree.nodes.get("Principled BSDF")
            shader.inputs["Roughness"].default_value = 1.0
            texture = material.node_tree.nodes.new("ShaderNodeTexImage")
            texture.image = atlas
            texture.interpolation = "Closest"
            material.node_tree.links.new(texture.outputs["Color"],
                                          shader.inputs["Base Color"])
            if material.name == "GLASS":
                material.diffuse_color[3] = .60
                shader.inputs["Alpha"].default_value = .60
                material.surface_render_method = "DITHERED"


def main():
    source = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesh", type=Path, required=True)
    parser.add_argument("--blend", type=Path, required=True)
    args = parser.parse_args(source)
    root = Path(__file__).resolve().parents[1]
    texture_path = root / "assets/textures/vehicles/municipal_cruiser_91d/body.png"
    args.mesh.parent.mkdir(parents=True, exist_ok=True)

    closed = build_vehicle(False)
    configure_materials([closed], texture_path)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print("CRUISER_91D_CLOSED", export_emesh(closed, args.mesh,
                                               "municipal_cruiser_91d"))

    body_open, driver_door, panes = build_vehicle(True)
    configure_materials([body_open, driver_door, *panes.values()], texture_path)
    print("CRUISER_91D_OPEN", export_emesh(
        body_open, args.mesh.with_name("body_open.emesh"),
        "municipal_cruiser_91d"))
    print("CRUISER_91D_DOOR", export_emesh(
        driver_door, args.mesh.with_name("driver_door.emesh"),
        "municipal_cruiser_91d"))
    for name in PANE_FILES:
        print("CRUISER_91D_PANE", name, export_emesh(
            panes[name], args.mesh.with_name(name + ".emesh"),
            "municipal_cruiser_91d"))
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend.with_name(
        "articulated.blend")))


if __name__ == "__main__":
    main()
