#!/usr/bin/env python3
"""Deterministic low-poly 1991 police sedan with articulated driver door."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from municipal_cruiser_91a_spec import (  # noqa: E402
    ATLAS_SIZE,
    DOOR,
    PANE_FILES,
    REGIONS,
    SHAPE,
    UV_PROJECTIONS,
    WHEELS,
)
from vesper_vx91_blender import VehicleBuilder, export_emesh  # noqa: E402


class CruiserBuilder(VehicleBuilder):
    def panel(self, name, vertices, material, desired):
        obj = super().panel(name, vertices, material, desired)
        # An isolated card has no connected volume from which Blender can infer
        # the authored outside.  Keep the requested direction explicitly.
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


def profile(y: float) -> tuple[float, float]:
    """Outer half-width and nearly-flat fender/hood shoulder height."""
    keys = (
        (-2.675, .98, .91), (-2.52, 1.02, .99), (-2.07, 1.04, 1.01),
        (-1.53, 1.04, 1.03), (-.99, 1.035, 1.01),
        (.84, 1.035, 1.02), (1.09, 1.04, 1.01),
        (1.63, 1.04, 1.04), (2.17, 1.035, 1.01),
        (2.52, 1.01, .97), (2.675, .98, .90),
    )
    for left, right in zip(keys, keys[1:]):
        if left[0] - 1e-7 <= y <= right[0] + 1e-7:
            t = (y - left[0]) / (right[0] - left[0])
            return tuple(left[i] + t * (right[i] - left[i]) for i in (1, 2))
    raise ValueError(y)


def body_stations() -> list[tuple[float, float]]:
    """Longitudinal stations with vertical feet and faceted open arches."""
    r = SHAPE["arch_long_radius"]
    h = SHAPE["arch_height_radius"]
    c = WHEELS["arch_y"]
    result: list[tuple[float, float]] = [(-2.675, .20), (-2.36, .20)]
    for axle in (WHEELS["rear_z"], WHEELS["front_z"]):
        result.extend(((axle - r, .20), (axle - r, c)))
        result.extend(
            (axle - r * math.cos(i * math.pi / 6),
             c + h * math.sin(i * math.pi / 6))
            for i in range(1, 6)
        )
        result.extend(((axle + r, c), (axle + r, .20)))
        if axle < 0:
            # These exact stations isolate the +X driver-front door skin.
            result.extend(((-.72, .20), (DOOR["rear_z"], .20),
                           (DOOR["front_z"], .20)))
    result.extend(((2.38, .20), (2.675, .20)))
    return sorted(result, key=lambda item: item[0])


def add_material(obj, builder: CruiserBuilder, name: str) -> int:
    if obj.data.materials.find(name) < 0:
        obj.data.materials.append(builder.material(name))
    return obj.data.materials.find(name)


def build_side_shell(builder: CruiserBuilder, side: float):
    rings = []
    for y, low in body_stations():
        width, top = profile(y)
        points = ((.66, low), (width, low + .035),
                  (width, top - .095), (.94 * width, top), (.72, top))
        rings.append((y, [(side * x, z) for x, z in points]))
    shell = builder.loft("IntegratedSideShell" + ("L" if side < 0 else "R"),
                         rings, "BODY_SIDE")
    top_index = add_material(shell, builder, "BODY_TOP")
    shadow_index = add_material(shell, builder, "SHADOW")
    for face in shell.data.polygons:
        if face.normal.z > .62:
            face.material_index = top_index
        elif face.normal.z < -.18:
            face.material_index = shadow_index
    if side > 0:
        # The outer, shoulder, and inboard skins above the sill are absent here.
        # The separate door closes this exact negative-space opening.
        bm = bmesh.new()
        bm.from_mesh(shell.data)
        doomed = []
        for face in bm.faces:
            centre = face.calc_center_median()
            if (DOOR["rear_z"] + 1e-5 < centre.y < DOOR["front_z"] - 1e-5
                    and centre.z > DOOR["sill_y"] - .015
                    and centre.x > .60):
                doomed.append(face)
        bmesh.ops.delete(bm, geom=doomed, context="FACES")
        bm.to_mesh(shell.data)
        bm.free()
        shell.data.update()
    return shell


def beam(builder: CruiserBuilder, name: str, start, end, width: float,
         material: str):
    """Capped triangular frame beam between arbitrary points."""
    direction = (Vector(end) - Vector(start)).normalized()
    reference = Vector((0, 0, 1)) if abs(direction.z) < .88 else Vector((0, 1, 0))
    across = direction.cross(reference).normalized() * width * .5
    depth = direction.cross(across).normalized() * width * .5
    section = (across, -.5 * across + .866 * depth,
               -.5 * across - .866 * depth)
    vertices = [tuple(Vector(point) + offset)
                for point in (start, end) for offset in section]
    faces = ((0, 2, 1), (3, 4, 5), (0, 1, 4, 3),
             (1, 2, 5, 4), (2, 0, 3, 5))
    return builder.add_mesh(name, vertices, list(faces), material)


def closed_pane(builder: CruiserBuilder, name: str, points, outward,
                thickness: float = .010):
    """Thin capped pane.  Alpha comes from the shared GLASS atlas cell."""
    desired = Vector(outward).normalized()
    front = [Vector(point) for point in points]
    if (front[1] - front[0]).cross(front[2] - front[0]).dot(desired) < 0:
        front.reverse()
    back = [point - desired * thickness for point in front]
    n = len(front)
    vertices = [tuple(point) for point in front + back]
    faces = [tuple(range(n)), tuple(reversed(range(n, 2 * n)))]
    faces.extend((i, (i + 1) % n, n + (i + 1) % n, n + i)
                 for i in range(n))
    return builder.add_mesh(name, vertices, faces, "GLASS")


def side_x(side: float, height: float) -> float:
    return side * (1.002 - (height - 1.02) * (.215 / .53))


def side_card(builder: CruiserBuilder, name: str, side: float, yz,
              material: str, x: float = 1.041):
    return builder.panel(name, [(side * x, y, z) for y, z in yz], material,
                         (side, 0, 0))


def faceted_bumper(builder: CruiserBuilder, name: str, front: bool):
    y0, y1 = ((2.59, 2.675) if front else (-2.675, -2.59))
    ring = [(-.91, .29), (-1.04, .35), (-1.04, .47), (-.94, .52),
            (.94, .52), (1.04, .47), (1.04, .35), (.91, .29)]
    return builder.loft(name, [(y0, ring), (y1, ring)], "METAL")


def steering_rim(builder: CruiserBuilder):
    vertices = []
    centre = Vector((.43, .57, 1.08))
    for depth in (-.014, .014):
        for radius in (.145, .105):
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


def add_interior(builder: CruiserBuilder):
    parts = []
    parts.append(builder.box("CabinFloor", (-.76, -1.23, .38),
                             (.76, .80, .44), "INTERIOR"))
    parts.append(builder.box("TransmissionTunnel", (-.13, -1.12, .44),
                             (.13, .66, .56), "INTERIOR"))
    parts.append(builder.box("Firewall", (-.77, .78, .44),
                             (.77, .84, 1.01), "SHADOW"))
    parts.append(builder.box("RearBulkhead", (-.77, -1.25, .44),
                             (.77, -1.19, 1.01), "SHADOW"))
    for row, y in (("Front", .17), ("Rear", -.78)):
        for suffix, x in (("L", -.43), ("R", .43)):
            parts.append(builder.box(row + "SeatCushion" + suffix,
                                     (x - .24, y - .25, .57),
                                     (x + .24, y + .23, .73), "INTERIOR"))
            parts.append(builder.box(row + "SeatBack" + suffix,
                                     (x - .24, y - .29, .71),
                                     (x + .24, y - .21, 1.23), "INTERIOR"))
    parts.append(builder.loft("Dashboard", [
        (.55, [(-.75, .91), (-.75, 1.13), (.75, 1.13), (.75, .91)]),
        (.75, [(-.75, .89), (-.75, 1.03), (.75, 1.03), (.75, .89)]),
    ], "INTERIOR"))
    parts.append(builder.box("GaugeBinnacle", (.20, .53, 1.08),
                             (.66, .69, 1.20), "BLACK"))
    parts.append(steering_rim(builder))
    parts.append(builder.box("SteeringColumn", (.40, .57, 1.04),
                             (.46, .75, 1.10), "METAL"))
    for x in (.33, .53):
        parts.append(builder.box("Pedal" + str(x), (x - .035, .64, .445),
                                 (x + .035, .73, .48), "METAL"))
    parts.append(builder.panel("RearViewMirror", [(-.15, .39, 1.39),
                                                   (.15, .39, 1.39),
                                                   (.15, .39, 1.49),
                                                   (-.15, .39, 1.49)],
                               "BLACK", (0, 1, 0)))
    return parts


def add_greenhouse(builder: CruiserBuilder, door_parts: list):
    body_parts = []
    roof_ring = [(-.76, 1.52), (-.72, 1.58), (.72, 1.58), (.76, 1.52)]
    body_parts.append(builder.loft("FlatFormalRoof", [(-.96, roof_ring),
                                                       (.39, roof_ring)],
                                   "BODY_TOP"))
    for side in (-1., 1.):
        suffix = "L" if side < 0 else "R"
        a_low = (side * .985, .84, 1.02)
        a_top = (side * .785, .39, 1.55)
        b_low = (side * .985, -.22, 1.02)
        b_top = (side * .785, -.22, 1.55)
        c_low = (side * .985, -1.25, 1.02)
        c_top = (side * .785, -.96, 1.55)
        for label, start, end in (
            ("APillar", a_low, a_top), ("BPillar", b_low, b_top),
            ("CPillar", c_low, c_top), ("RoofRailFront", a_top, b_top),
            ("RoofRailRear", b_top, c_top), ("WindowSillFront", a_low, b_low),
            ("WindowSillRear", b_low, c_low),
        ):
            body_parts.append(beam(builder, label + suffix, start, end, .064,
                                   "BODY_TOP" if "RoofRail" in label else "BODY_SIDE"))

    # Broad top and bottom windshield/rear-window rails tie both sides together.
    for label, start, end in (
        ("WindshieldHeader", (-.785, .39, 1.55), (.785, .39, 1.55)),
        ("WindshieldCowl", (-.985, .84, 1.02), (.985, .84, 1.02)),
        ("RearHeader", (-.785, -.96, 1.55), (.785, -.96, 1.55)),
        ("RearShelfRail", (-.985, -1.25, 1.02), (.985, -1.25, 1.02)),
    ):
        body_parts.append(beam(builder, label, start, end, .065, "BODY_SIDE"))

    # The +X front door owns its lower shell, complete frame, paint card,
    # handle, and mirror.  Its glazing remains a separate exported pane.
    door_parts.append(builder.box("DriverDoorShell", (.965, -.22, .50),
                                  (1.04, .84, 1.02), "DOOR_WHITE"))
    driver_edges = (
        ((.982, .79, 1.03), (.795, .36, 1.54)),
        ((.982, -.18, 1.03), (.795, -.18, 1.54)),
        ((.982, .79, 1.03), (.982, -.18, 1.03)),
        ((.795, .36, 1.54), (.795, -.18, 1.54)),
    )
    for index, (start, end) in enumerate(driver_edges):
        door_parts.append(beam(builder, "DriverDoorFrame" + str(index),
                               start, end, .052, "DOOR_WHITE"))
    door_parts.append(side_card(builder, "DriverPoliceMark", 1,
                                [(-.18, .55), (.80, .55), (.80, .98),
                                 (-.18, .98)], "POLICE"))
    door_parts.append(builder.box("DriverHandle", (1.025, -.08, .92),
                                  (1.04, .12, .97), "METAL"))
    door_parts.append(builder.box("DriverMirror", (.982, .56, 1.02),
                                  (1.04, .76, 1.15), "RUBBER"))
    return body_parts


def add_static_doors(builder: CruiserBuilder):
    parts = []
    # Passenger front and both rear doors are shallow, capped skins over the
    # hollow cabin.  The +X front slot is deliberately reserved for the door.
    for side in (-1., 1.):
        suffix = "L" if side < 0 else "R"
        if side < 0:
            parts.append(side_card(builder, "PassengerFrontDoorShell", side,
                                   [(-.22, .50), (.84, .50), (.84, 1.02),
                                    (-.22, 1.02)], "DOOR_WHITE"))
            parts.append(side_card(builder, "PassengerPoliceMark", side,
                                   [(-.18, .55), (.80, .55), (.80, .98),
                                    (-.18, .98)], "POLICE"))
            parts.append(builder.box("PassengerMirror", (-1.04, .56, 1.02),
                                     (-.982, .76, 1.15), "RUBBER"))
        parts.append(side_card(builder, "RearDoorShell" + suffix, side,
                               [(-1.25, .50), (-.22, .50), (-.22, 1.02),
                                (-1.25, 1.02)], "DOOR_WHITE"))
        for label, y in (("Rear", -1.18), *(([("Front", -.12)]) if side < 0 else [])):
            parts.append(side_card(builder, label + "Handle" + suffix, side,
                                   [(y, .92), (y + .19, .92),
                                    (y + .19, .97), (y, .97)], "METAL"))
        side_card(builder, "FrontDoorSeam" + suffix, side,
                  [(.82, .50), (.84, .50), (.84, 1.01), (.82, 1.01)],
                  "SEAM", 1.04)
        side_card(builder, "RearDoorSeam" + suffix, side,
                  [(-.24, .50), (-.22, .50), (-.22, 1.01), (-.24, 1.01)],
                  "SEAM", 1.04)
    return parts


def add_panes(builder: CruiserBuilder):
    panes = {}
    panes["windshield"] = closed_pane(builder, "windshield", [
        (-.90, .815, 1.075), (.90, .815, 1.075),
        (.735, .415, 1.515), (-.735, .415, 1.515),
    ], (0, 1, .5))
    panes["rear_glass"] = closed_pane(builder, "rear_glass", [
        (.735, -.975, 1.515), (-.735, -.975, 1.515),
        (-.90, -1.225, 1.075), (.90, -1.225, 1.075),
    ], (0, -1, .45))
    front_yz = ((.745, 1.075), (-.165, 1.075),
                (-.165, 1.495), (.385, 1.495))
    rear_yz = ((-.275, 1.075), (-1.185, 1.075),
               (-.925, 1.495), (-.275, 1.495))
    for side, front_name, rear_name in (
        (-1., "passenger_glass", "passenger_rear_glass"),
        (1., "driver_glass", "driver_rear_glass"),
    ):
        panes[front_name] = closed_pane(
            builder, front_name,
            [(side_x(side, z), y, z) for y, z in front_yz], (side, 0, 0))
        panes[rear_name] = closed_pane(
            builder, rear_name,
            [(side_x(side, z), y, z) for y, z in rear_yz], (side, 0, 0))
    return panes


def add_exterior_details(builder: CruiserBuilder):
    parts = []
    parts.extend((faceted_bumper(builder, "FrontChromeBumper", True),
                  faceted_bumper(builder, "RearChromeBumper", False)))
    parts.append(builder.box("FrontReceiver", (-.96, 2.61, .48),
                             (.96, 2.675, .96), "FRONT"))
    parts.append(builder.box("RearReceiver", (-.96, -2.675, .48),
                             (.96, -2.61, .96), "REAR"))
    for side in (-1., 1.):
        for index, (inner, outer) in enumerate(((.42, .65), (.68, .94))):
            x0, x1 = sorted((side * inner, side * outer))
            parts.append(builder.panel(f"SealedBeam{side}{index}",
                                       [(x0, 2.675, .70), (x1, 2.675, .70),
                                        (x1, 2.675, .88), (x0, 2.675, .88)],
                                       "HEADLIGHT", (0, 1, 0)))
        x0, x1 = sorted((side * .70, side * .94))
        parts.append(builder.panel("FrontIndicator" + str(side),
                                   [(x0, 2.675, .55), (x1, 2.675, .55),
                                    (x1, 2.675, .66), (x0, 2.675, .66)],
                                   "AMBER", (0, 1, 0)))
        x0, x1 = sorted((side * .58, side * .94))
        parts.append(builder.panel("RearTail" + str(side),
                                   [(x1, -2.675, .68), (x0, -2.675, .68),
                                    (x0, -2.675, .90), (x1, -2.675, .90)],
                                   "TAIL_RED", (0, -1, 0)))
        x0, x1 = sorted((side * .39, side * .56))
        parts.append(builder.panel("RearReverse" + str(side),
                                   [(x1, -2.675, .68), (x0, -2.675, .68),
                                    (x0, -2.675, .90), (x1, -2.675, .90)],
                                   "REVERSE", (0, -1, 0)))
    for x in (-.30, -.18, -.06, .06, .18, .30):
        parts.append(builder.panel("GrilleBar" + str(x),
                                   [(x - .018, 2.675, .57),
                                    (x + .018, 2.675, .57),
                                    (x + .018, 2.675, .90),
                                    (x - .018, 2.675, .90)],
                                   "METAL", (0, 1, 0)))
    parts.append(builder.panel("GrilleTop", [(-.36, 2.675, .87),
                                               (.36, 2.675, .87),
                                               (.36, 2.675, .91),
                                               (-.36, 2.675, .91)],
                               "METAL", (0, 1, 0)))
    parts.append(builder.panel("GrilleBottom", [(-.36, 2.675, .56),
                                                  (.36, 2.675, .56),
                                                  (.36, 2.675, .60),
                                                  (-.36, 2.675, .60)],
                               "METAL", (0, 1, 0)))

    # Square, simple push bar that remains inside the 5.35 m overall length.
    for x in (-.39, .39):
        parts.append(builder.box("PushUpright" + str(x),
                                 (x - .04, 2.625, .38),
                                 (x + .04, 2.675, .93), "RUBBER"))
    for z in (.54, .82):
        parts.append(builder.box("PushCross" + str(z),
                                 (-.58, 2.63, z - .04),
                                 (.58, 2.675, z + .04), "RUBBER"))

    # Roof lightbar top is the exact requested 1.78 m source height.
    for x in (-.48, .48):
        parts.append(builder.box("LightbarFoot" + str(x),
                                 (x - .045, -.17, 1.58),
                                 (x + .045, -.04, 1.63), "RUBBER"))
    parts.append(builder.box("LightbarBase", (-.82, -.25, 1.62),
                             (.82, .03, 1.65), "RUBBER"))
    parts.append(builder.box("LightbarRed", (-.79, -.235, 1.65),
                             (-.06, .015, 1.78), "LIGHTBAR_RED"))
    parts.append(builder.box("LightbarBlue", (.06, -.235, 1.65),
                             (.79, .015, 1.78), "LIGHTBAR_BLUE"))
    parts.append(builder.box("LightbarSpeaker", (-.055, -.225, 1.65),
                             (.055, .005, 1.76), "BLACK"))
    for side in (-1., 1.):
        # Twin A-pillar spotlights with low-facet round housings.
        parts.append(builder.cylinder("Spotlight" + str(side),
                                     (side * .91, .60, 1.37), .105, .075, 8,
                                     "METAL"))
        parts.append(builder.cylinder("SpotlightLens" + str(side),
                                     (side * .91, .642, 1.37), .083, .014, 8,
                                     "HEADLIGHT"))
        parts.append(beam(builder, "SpotlightArm" + str(side),
                          (side * .89, .55, 1.27),
                          (side * .91, .59, 1.34), .035, "METAL"))
    return parts


def map_uvs(obj):
    mesh = obj.data
    uv = mesh.uv_layers.new(name="UVMap")
    for face in mesh.polygons:
        material = mesh.materials[face.material_index].name
        x0, y0, x1, y1 = REGIONS[material]
        points = [mesh.vertices[mesh.loops[i].vertex_index].co
                  for i in face.loop_indices]
        normal = face.normal
        if material in UV_PROJECTIONS:
            axes, projection = UV_PROJECTIONS[material]
            bounds = projection
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
                              max(0., min(1., (point[axis] - low) / (high - low))))
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
    builder = CruiserBuilder()
    door_parts = []
    body_parts = [build_side_shell(builder, -1.), build_side_shell(builder, 1.)]
    body_parts.append(builder.box("CentralChassis", (-.47, -2.61, .20),
                                  (.47, 2.61, .38), "SHADOW"))
    # Broad centre strips stay over both axles; wheel pockets cut only the sides.
    for name, low, high in (("LongFlatHood", .84, 2.675),
                            ("SquareDeck", -2.675, -1.25)):
        ys = [low, high]
        ys.extend(axle for axle in (WHEELS["rear_z"], WHEELS["front_z"])
                  if low < axle < high)
        sections = []
        for y in sorted(set(ys)):
            top = profile(y)[1]
            sections.append((y, [(-.72, top - .035), (-.72, top),
                                 (.72, top), (.72, top - .035)]))
        body_parts.append(builder.loft(name, sections, "BODY_TOP"))
    body_parts.extend(add_interior(builder))
    body_parts.extend(add_greenhouse(builder, door_parts))
    body_parts.extend(add_static_doors(builder))
    panes = add_panes(builder)
    body_parts.extend(add_exterior_details(builder))

    # Categorize from identity rather than names so later detail additions do
    # not accidentally leak into the wrong articulated export.
    pane_objects = set(panes.values())
    door_objects = set(door_parts)
    body_objects = [obj for obj in builder.objects
                    if obj not in pane_objects and obj not in door_objects]
    for obj in builder.objects:
        clean_object(obj)
    add_wheel_anchors()
    if not articulated:
        closed = join_objects(builder.objects, "BODY", "MunicipalCruiser91ABody")
        assert len([obj for obj in bpy.context.scene.objects if obj.type == "MESH"]) == 1
        return closed
    body_open = join_objects(body_objects, "BODY_OPEN",
                             "MunicipalCruiser91AOpenBody")
    driver_door = join_objects(door_parts, "DRIVER_DOOR",
                               "MunicipalCruiser91ADriverDoor")
    assert set(panes) == set(PANE_FILES)
    assert len([obj for obj in bpy.context.scene.objects if obj.type == "MESH"]) == 8
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
                material.diffuse_color[3] = .62
                shader.inputs["Alpha"].default_value = .62
                material.surface_render_method = "DITHERED"


def main():
    source = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesh", type=Path, required=True)
    parser.add_argument("--blend", type=Path, required=True)
    args = parser.parse_args(source)
    root = Path(__file__).resolve().parents[1]
    texture_path = root / "assets/textures/vehicles/municipal_cruiser_91a/body.png"
    args.mesh.parent.mkdir(parents=True, exist_ok=True)

    closed = build_vehicle(False)
    configure_materials([closed], texture_path)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print("CRUISER_91A_CLOSED", export_emesh(closed, args.mesh,
                                                   "municipal_cruiser_91a"))

    body_open, driver_door, panes = build_vehicle(True)
    configure_materials([body_open, driver_door, *panes.values()], texture_path)
    print("CRUISER_91A_OPEN", export_emesh(body_open,
                                              args.mesh.with_name("body_open.emesh"),
                                              "municipal_cruiser_91a"))
    print("CRUISER_91A_DOOR", export_emesh(driver_door,
                                              args.mesh.with_name("driver_door.emesh"),
                                              "municipal_cruiser_91a"))
    for name in PANE_FILES:
        print("CRUISER_91A_PANE", name,
              export_emesh(panes[name], args.mesh.with_name(name + ".emesh"),
                           "municipal_cruiser_91a"))
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend.with_name("articulated.blend")))


if __name__ == "__main__":
    main()
