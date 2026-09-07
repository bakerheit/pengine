#!/usr/bin/env python3
"""Build the original 1991 Municipal Cruiser 91-B articulated sedan.

The shell is authored explicitly around four side wheel openings.  It does not
contain wheels, inherit another vehicle silhouette, or hide an opaque cabin
behind the six separate panes.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from harrow_workman_blender import PickupBuilder as VehicleBuilder
from municipal_cruiser_91b_spec import (ATLAS_SIZE, GLASS_NAMES, REGIONS,
                                        UV_PROJECTIONS, WHEELS)
from vesper_vx91_blender import export_emesh


DRIVER_DOOR_REAR = -0.30
DRIVER_DOOR_FRONT = 0.82
REAR_DOOR_REAR = -0.94


def interpolate(keys, y):
    for first, second in zip(keys, keys[1:]):
        if first[0] - 1e-8 <= y <= second[0] + 1e-8:
            t = (y - first[0]) / (second[0] - first[0])
            return tuple(first[i] + t * (second[i] - first[i])
                         for i in range(1, len(first)))
    raise ValueError(y)


def body_profile(y):
    """Outer half-width and belt/upper-body height at a longitudinal station."""
    return interpolate([
        (-2.58, .91, .84), (-2.35, .985, .91), (-1.92, 1.015, .97),
        (-1.30, 1.015, .99), (-.30, 1.015, 1.01), (.84, 1.015, .99),
        (1.30, 1.020, .98), (1.95, 1.015, .92), (2.35, .975, .84),
        (2.58, .90, .78),
    ], y)


def cabin_side_x(height):
    return 1.015 - (height - 1.02) * .50


def arch_stations(start, end, axle=None):
    """Ordered rocker/arch boundary, retaining the vertical arch feet."""
    result = [(start, .20)]
    if axle is not None:
        radius = .54
        before = [y for y in (-2.35, -1.30, -.94, .84, 1.30, 1.95, 2.35)
                  if start < y < axle - radius and y < end]
        result.extend((y, .20) for y in before)
        result.extend(((axle - radius, .20), (axle - radius, .42)))
        # Five broad facets are deliberate: smooth enough for the transitional
        # aero read, still chunky beside Legacy Car 5 at gameplay distance.
        result.extend((axle - radius * math.cos(index * math.pi / 5),
                       .42 + .45 * math.sin(index * math.pi / 5))
                      for index in range(1, 5))
        result.extend(((axle + radius, .42), (axle + radius, .20)))
        after = [y for y in (-2.35, -1.30, -.94, .84, 1.30, 1.95, 2.35)
                 if axle + radius < y < end]
        result.extend((y, .20) for y in after)
    result.append((end, .20))
    return result


def side_ring(side, y, low, forced_top=None):
    width, natural_top = body_profile(y)
    top = natural_top if forced_top is None else forced_top
    outer_low = min(top - .075, max(low + .055, .28))
    return [
        (side * .48, low),
        (side * width, outer_low),
        (side * width, top - .075),
        (side * .88, top),
        (side * .68, top - .004),
    ]


def set_face_material(builder, obj, polygon, name):
    if name not in obj.data.materials:
        obj.data.materials.append(builder.material(name))
    polygon.material_index = obj.data.materials.find(name)


def side_shell(builder, name, side, stations, fixed, forced_top=None):
    obj = builder.loft(name, [(y, side_ring(side, y, low, forced_top))
                              for y, low in stations], "BODY_SIDE")
    for polygon in obj.data.polygons:
        points = [obj.data.vertices[index].co for index in polygon.vertices]
        mean_y = sum(point.y for point in points) / len(points)
        if polygon.normal.z > .62:
            set_face_material(builder, obj, polygon, "BODY_TOP")
        elif polygon.normal.z < -.20:
            set_face_material(builder, obj, polygon, "NAVY")
        elif abs(polygon.normal.y) > .82:
            if mean_y > 2.56:
                set_face_material(builder, obj, polygon, "BODY_FRONT")
            elif mean_y < -2.56:
                set_face_material(builder, obj, polygon, "BODY_REAR")
            else:
                set_face_material(builder, obj, polygon, "INTERIOR")
    fixed.append(obj)
    return obj


def beam(builder, name, start, end, width, material):
    """Capped triangular-section structural member between arbitrary points."""
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


def closed_pane(builder, name, points, material, outward, thickness=.010):
    """Thin capped pane; runtime loads each one with the glass material."""
    desired = Vector(outward).normalized()
    front = [Vector(point) for point in points]
    normal = (front[1] - front[0]).cross(front[2] - front[0])
    if normal.dot(desired) < 0:
        front.reverse()
    back = [point - desired * thickness for point in front]
    count = len(front)
    vertices = [tuple(point) for point in front + back]
    faces = [tuple(range(count)), tuple(reversed(range(count, count * 2)))]
    faces.extend((index, (index + 1) % count,
                  count + (index + 1) % count, count + index)
                 for index in range(count))
    return builder.add_mesh(name, vertices, faces, material)


def side_prism(builder, name, side, yz, thickness, material, taper=True):
    """Capped thin side shell with an outside face and a true inside face."""
    outer = [(side * (cabin_side_x(z) if taper else 1.015), y, z)
             for y, z in yz]
    inner = [(x - side * thickness, y, z) for x, y, z in outer]
    count = len(outer)
    faces = [tuple(range(count)), tuple(reversed(range(count, count * 2)))]
    faces.extend((index, (index + 1) % count,
                  count + (index + 1) % count, count + index)
                 for index in range(count))
    return builder.add_mesh(name, outer + inner, faces, material)


def tracked_box(builder, target, name, lo, hi, material):
    obj = builder.box(name, lo, hi, material)
    target.append(obj)
    return obj


def tracked_panel(builder, target, name, points, material, desired):
    obj = builder.panel(name, points, material, desired)
    target.append(obj)
    return obj


def top_loft(builder, target, name, sections, material="BODY_TOP"):
    rings = []
    for y, width, height, crown in sections:
        rings.append((y, [(-width, height - .055), (-width, height),
                          (-.27, crown), (.27, crown),
                          (width, height), (width, height - .055)]))
    obj = builder.loft(name, rings, material)
    target.append(obj)
    return obj


def steering_wheel(builder, fixed):
    centre = Vector((.43, .49, .94))
    segments = 8
    vertices = []
    for depth in (-.014, .014):
        for radius in (.145, .105):
            for index in range(segments):
                angle = math.tau * index / segments
                vertices.append((centre.x + radius * math.cos(angle),
                                 centre.y + depth,
                                 centre.z + radius * math.sin(angle)))
    faces = []
    outer_front, inner_front = 0, segments
    outer_back, inner_back = segments * 2, segments * 3
    for index in range(segments):
        nxt = (index + 1) % segments
        faces.extend(((outer_front + index, outer_front + nxt,
                       outer_back + nxt, outer_back + index),
                      (inner_front + nxt, inner_front + index,
                       inner_back + index, inner_back + nxt),
                      (outer_front + index, inner_front + index,
                       inner_front + nxt, outer_front + nxt),
                      (outer_back + nxt, inner_back + nxt,
                       inner_back + index, outer_back + index)))
    fixed.append(builder.add_mesh("SteeringWheelRim", vertices, faces, "RUBBER"))
    tracked_box(builder, fixed, "SteeringHorizontalSpoke",
                (.30, .472, .928), (.56, .508, .952), "METAL")
    tracked_box(builder, fixed, "SteeringLowerSpoke",
                (.415, .472, .80), (.445, .508, .94), "METAL")
    tracked_box(builder, fixed, "SteeringColumn",
                (.405, .49, .90), (.455, .67, .98), "RUBBER")


def make_interior(builder, fixed):
    tracked_box(builder, fixed, "CabinFloor", (-.79, -1.24, .32),
                (.79, .78, .38), "INTERIOR")
    tracked_box(builder, fixed, "Firewall", (-.79, .72, .38),
                (.79, .80, .98), "INTERIOR")
    tracked_box(builder, fixed, "RearCabinBulkhead", (-.79, -1.30, .38),
                (.79, -1.23, 1.00), "INTERIOR")
    # Low, broad dash with a faceted padded brow.
    obj = builder.loft("Dashboard", [
        (.49, [(-.78, .78), (-.78, .98), (-.68, 1.05),
               (.68, 1.05), (.78, .98), (.78, .78)]),
        (.70, [(-.78, .76), (-.78, .93), (-.67, 1.00),
               (.67, 1.00), (.78, .93), (.78, .76)]),
    ], "INTERIOR")
    fixed.append(obj)
    tracked_box(builder, fixed, "RadioConsole", (-.12, .38, .44),
                (.12, .61, .92), "RUBBER")
    tracked_panel(builder, fixed, "RadioFace",
                  [(-.105, .616, .66), (.105, .616, .66),
                   (.105, .616, .86), (-.105, .616, .86)],
                  "METAL", (0, 1, 0))
    # Four distinct cushions plus a rear bench make the hollow cabin legible.
    for suffix, x in (("Driver", .43), ("Passenger", -.43)):
        tracked_box(builder, fixed, suffix + "SeatBase",
                    (x - .25, -.39, .39), (x + .25, .13, .58), "INTERIOR")
        tracked_box(builder, fixed, suffix + "SeatCushion",
                    (x - .24, -.36, .58), (x + .24, .10, .64), "WHITE")
        obj = builder.loft(suffix + "SeatBack", [
            (-.40, [(x - .25, .60), (x - .24, 1.18),
                    (x + .24, 1.18), (x + .25, .60)]),
            (-.31, [(x - .24, .60), (x - .22, 1.16),
                    (x + .22, 1.16), (x + .24, .60)]),
        ], "INTERIOR")
        fixed.append(obj)
        tracked_panel(builder, fixed, suffix + "Headrest",
                      [(x - .17, -.275, 1.14), (x + .17, -.275, 1.14),
                       (x + .17, -.275, 1.34), (x - .17, -.275, 1.34)],
                      "INTERIOR", (0, 1, 0))
    tracked_box(builder, fixed, "RearBenchBase", (-.70, -1.16, .39),
                (.70, -.65, .61), "INTERIOR")
    tracked_box(builder, fixed, "RearBenchCushion", (-.68, -1.13, .61),
                (.68, -.68, .67), "WHITE")
    tracked_box(builder, fixed, "RearBenchBack", (-.70, -1.24, .64),
                (.70, -1.15, 1.16), "INTERIOR")
    steering_wheel(builder, fixed)
    for x in (.33, .53):
        tracked_panel(builder, fixed, "Pedal" + str(x),
                      [(x - .04, .68, .416), (x + .04, .68, .416),
                       (x + .04, .55, .386), (x - .04, .55, .386)],
                      "METAL", (0, 1, 1))
    # Sparse period partition frame behind the front buckets.
    for x in (-.52, .52):
        fixed.append(beam(builder, "PartitionPost" + str(x),
                          (x, -.49, .70), (x, -.49, 1.38), .025, "RUBBER"))
    for z in (.90, 1.30):
        fixed.append(beam(builder, "PartitionRail" + str(z),
                          (-.66, -.49, z), (.66, -.49, z), .022, "RUBBER"))


def make_cabin(builder, fixed, door_parts, glass):
    # Roof is a thin faceted cap, never a solid greenhouse.
    roof = builder.loft("RoofCap", [
        (-.98, [(-.77, 1.48), (-.67, 1.56), (.67, 1.56), (.77, 1.48)]),
        (.34, [(-.75, 1.48), (-.65, 1.56), (.65, 1.56), (.75, 1.48)]),
    ], "BODY_TOP")
    fixed.append(roof)

    windshield_outer = [(-.88, .82, 1.02), (.88, .82, 1.02),
                        (.69, .32, 1.52), (-.69, .32, 1.52)]
    windshield_glass = [(-.82, .785, 1.075), (.82, .785, 1.075),
                        (.64, .35, 1.47), (-.64, .35, 1.47)]
    rear_outer = [(.85, -1.30, 1.00), (-.85, -1.30, 1.00),
                  (-.70, -.98, 1.52), (.70, -.98, 1.52)]
    rear_glass = [(.79, -1.265, 1.06), (-.79, -1.265, 1.06),
                  (-.65, -1.005, 1.47), (.65, -1.005, 1.47)]

    for index in (1, 2, 3):
        fixed.append(beam(builder, "WindshieldFrame" + str(index),
                          windshield_outer[index],
                          windshield_outer[(index + 1) % 4], .064, "WHITE"))
    for index in (0, 1, 2):
        fixed.append(beam(builder, "RearWindowFrame" + str(index),
                          rear_outer[index], rear_outer[(index + 1) % 4],
                          .066, "WHITE"))

    glass["windshield"] = closed_pane(builder, "windshield", windshield_glass,
                                       "GLASS_FRONT", (0, 1, .55))
    glass["rear_glass"] = closed_pane(builder, "rear_glass", rear_glass,
                                       "GLASS_REAR", (0, -1, .55))

    front_window = [(.75, 1.055), (-.24, 1.055),
                    (-.24, 1.475), (.29, 1.475)]
    rear_window = [(-.34, 1.055), (-1.18, 1.055),
                   (-.94, 1.475), (-.34, 1.475)]

    for side in (-1, 1):
        suffix = "Passenger" if side < 0 else "Driver"
        # Fixed A/B/C pillars and roof rail define true window apertures.
        fixed.append(beam(builder, suffix + "APillar",
                          (side * .88, .82, 1.02),
                          (side * .69, .32, 1.52), .068, "WHITE"))
        fixed.append(beam(builder, suffix + "BPillar",
                          (side * cabin_side_x(1.02), -.29, 1.02),
                          (side * cabin_side_x(1.50), -.29, 1.50),
                          .072, "RUBBER"))
        fixed.append(beam(builder, suffix + "RoofRail",
                          (side * .69, .32, 1.52),
                          (side * .70, -.98, 1.52), .060, "WHITE"))
        c_pillar = side_prism(builder, suffix + "BroadCPillar", side,
                              [(-1.29, 1.02), (-.94, 1.50),
                               (-.76, 1.50), (-1.10, 1.02)], .052, "WHITE")
        fixed.append(c_pillar)

        front_points = [(side * cabin_side_x(z), y, z)
                        for y, z in front_window]
        rear_points = [(side * cabin_side_x(z), y, z)
                       for y, z in rear_window]
        front_name = "passenger_glass" if side < 0 else "driver_glass"
        rear_name = "passenger_rear_glass" if side < 0 else "driver_rear_glass"
        glass[front_name] = closed_pane(builder, front_name, front_points,
                                        "GLASS_SIDE", (side, 0, 0))
        glass[rear_name] = closed_pane(builder, rear_name, rear_points,
                                       "GLASS_SIDE", (side, 0, 0))

        if side > 0:
            # Door frame and mirror are independently moving with the +X skin.
            for index in range(4):
                door_parts.append(beam(builder, "DriverDoorWindowFrame" + str(index),
                                       front_points[index],
                                       front_points[(index + 1) % 4],
                                       .040, "WHITE"))
        # Passenger glazing sits within the fixed A/B/roof/belt structure.
        # Only the moving +X door needs its own duplicate capped frame.


def make_exterior_details(builder, fixed, door_parts):
    # Flush rectangular composite lamps at the faceted nose.
    for side in (-1, 1):
        inner, outer = sorted((side * .50, side * .94))
        lamp_inner, lamp_outer = sorted((side * .50, side * .83))
        amber_inner, amber_outer = sorted((side * .83, side * .94))
        tracked_box(builder, fixed, "Headlamp" + str(side),
                    (lamp_inner, 2.565, .56), (lamp_outer, 2.585, .79),
                    "HEADLIGHT")
        tracked_box(builder, fixed, "FrontIndicator" + str(side),
                    (amber_inner, 2.565, .56), (amber_outer, 2.585, .79),
                    "AMBER")
        # The union of red/reverse cells occupies the declared rear receiver.
        tail_a, tail_b = sorted((side * .58, side * .80))
        reverse_a, reverse_b = sorted((side * .80, side * .90))
        tail2_a, tail2_b = sorted((side * .90, side * .94))
        for index, (a, c) in enumerate(((tail_a, tail_b), (tail2_a, tail2_b))):
            tracked_box(builder, fixed, "TailLamp" + str((side, index)),
                        (a, -2.585, .56), (c, -2.565, .80), "TAIL")
        tracked_box(builder, fixed, "ReverseLamp" + str(side),
                    (reverse_a, -2.585, .56), (reverse_b, -2.565, .80),
                    "REVERSE")
        tracked_panel(builder, fixed, "FrontSideMarker" + str(side),
                      [(side * 1.019, y, z) for y, z in
                       ((1.96, .62), (2.16, .62), (2.16, .72), (1.96, .72))],
                      "AMBER", (side, 0, 0))

    tracked_box(builder, fixed, "FrontGrille", (-.47, 2.565, .49),
                (.47, 2.585, .76), "RUBBER")
    for z in (.535, .61, .685):
        tracked_panel(builder, fixed, "GrilleBar" + str(z),
                      [(-.44, 2.593, z), (.44, 2.593, z),
                       (.44, 2.593, z + .022), (-.44, 2.593, z + .022)],
                      "METAL", (0, 1, 0))
    tracked_box(builder, fixed, "RearPlateRecess", (-.32, -2.592, .48),
                (.32, -2.565, .70), "RUBBER")

    # Aero bumpers stay inside the exact 5.30 x 2.08 m overall contract.
    tracked_box(builder, fixed, "FrontAeroBumper", (-1.04, 2.54, .24),
                (1.04, 2.65, .43), "WHITE")
    tracked_box(builder, fixed, "RearAeroBumper", (-1.04, -2.65, .24),
                (1.04, -2.54, .43), "WHITE")
    tracked_panel(builder, fixed, "FrontRubStrip",
                  [(-.96, 2.651, .32), (.96, 2.651, .32),
                   (.96, 2.651, .39), (-.96, 2.651, .39)],
                  "RUBBER", (0, 1, 0))
    tracked_panel(builder, fixed, "RearRubStrip",
                  [(.96, -2.651, .32), (-.96, -2.651, .32),
                   (-.96, -2.651, .39), (.96, -2.651, .39)],
                  "RUBBER", (0, -1, 0))

    # Low two-rail push bar.  It never hides the full lamp face.
    for x in (-.41, .41):
        fixed.append(beam(builder, "PushPost" + str(x),
                          (x, 2.615, .38), (x, 2.615, .76), .07, "RUBBER"))
    for z in (.44, .69):
        fixed.append(beam(builder, "PushRail" + str(z),
                          (-.60, 2.62, z), (.60, 2.62, z), .06, "METAL"))

    # Split lightbar with a restrained dark centre gap and capped feet.
    for x in (-.50, .50):
        tracked_box(builder, fixed, "LightbarFoot" + str(x),
                    (x - .06, -.10, 1.56), (x + .06, .08, 1.60), "RUBBER")
    tracked_box(builder, fixed, "LightbarBase", (-.80, -.20, 1.58),
                (.80, .14, 1.62), "RUBBER")
    tracked_box(builder, fixed, "LightbarRed", (-.76, -.18, 1.62),
                (-.08, .12, 1.77), "LIGHTBAR_RED")
    tracked_box(builder, fixed, "LightbarBlue", (.08, -.18, 1.62),
                (.76, .12, 1.77), "LIGHTBAR_BLUE")
    tracked_box(builder, fixed, "LightbarSpeaker", (-.075, -.17, 1.62),
                (.075, .11, 1.73), "RUBBER")

    # Side rub strips, period rectangular handles and compact capped mirrors.
    for side in (-1, 1):
        target = door_parts if side > 0 else fixed
        if side > 0:
            intervals = [(-.22, .68)]
        else:
            intervals = [(-.90, -.34), (-.22, .68)]
        for start, end in intervals:
            tracked_panel(builder, target, "DoorRubStrip" + str((side, start)),
                          [(side * 1.019, start, .60), (side * 1.019, end, .60),
                           (side * 1.019, end, .65), (side * 1.019, start, .65)],
                          "RUBBER", (side, 0, 0))
        handles = [(-.08, target)] if side > 0 else [(-.08, fixed), (-.72, fixed)]
        for y, owner in handles:
            tracked_panel(builder, owner, "DoorHandle" + str((side, y)),
                          [(side * 1.041, y - .10, .86),
                           (side * 1.041, y + .10, .86),
                           (side * 1.041, y + .10, .91),
                           (side * 1.041, y - .10, .91)],
                          "METAL", (side, 0, 0))

    # Rear driver handle is fixed because only the driver-front door opens.
    tracked_panel(builder, fixed, "RearDriverHandle",
                  [(1.041, -.82, .86), (1.041, -.62, .86),
                   (1.041, -.62, .91), (1.041, -.82, .91)],
                  "METAL", (1, 0, 0))

    tracked_box(builder, door_parts, "DriverMirror",
                (1.012, .56, 1.04), (1.04, .79, 1.16), "RUBBER")
    tracked_panel(builder, door_parts, "DriverMirrorGlass",
                  [(1.041, .58, 1.06), (1.041, .77, 1.06),
                   (1.041, .77, 1.14), (1.041, .58, 1.14)],
                  "METAL", (1, 0, 0))
    tracked_box(builder, fixed, "PassengerMirror",
                (-1.04, .56, 1.04), (-1.012, .79, 1.16), "RUBBER")
    tracked_panel(builder, fixed, "PassengerMirrorGlass",
                  [(-1.041, .77, 1.06), (-1.041, .58, 1.06),
                   (-1.041, .58, 1.14), (-1.041, .77, 1.14)],
                  "METAL", (-1, 0, 0))

    # Exact text receivers are filled only after imagegen reduction.
    tracked_panel(builder, door_parts, "DriverPoliceReceiver",
                  [(1.021, -.22, .61), (1.021, .65, .61),
                   (1.021, .65, .91), (1.021, -.22, .91)],
                  "POLICE_DRIVER", (1, 0, 0))
    tracked_panel(builder, fixed, "PassengerPoliceReceiver",
                  [(-1.021, .65, .61), (-1.021, -.22, .61),
                   (-1.021, -.22, .91), (-1.021, .65, .91)],
                  "POLICE_PASSENGER", (-1, 0, 0))


def map_object(obj):
    mesh = obj.data
    if mesh.uv_layers:
        mesh.uv_layers.remove(mesh.uv_layers[0])
    uv = mesh.uv_layers.new(name="UVMap")

    def default_axes(normal):
        if abs(normal.z) >= max(abs(normal.x), abs(normal.y)):
            return 0, 1
        if abs(normal.y) > abs(normal.x):
            return 0, 2
        return 1, 2

    for polygon in mesh.polygons:
        material = mesh.materials[polygon.material_index].name
        if material not in REGIONS:
            raise KeyError((obj.name, material))
        x0, y0, x1, y1 = REGIONS[material]
        points = [mesh.vertices[mesh.loops[index].vertex_index].co
                  for index in polygon.loop_indices]
        if material in UV_PROJECTIONS:
            axes, pairs = UV_PROJECTIONS[material]
            bounds = pairs
        else:
            axes = default_axes(polygon.normal)
            bounds = tuple((min(point[axis] for point in points),
                            max(point[axis] for point in points)) for axis in axes)
        coordinates = []
        for point in points:
            values = [(point[axis] - lo) / (hi - lo) if hi - lo > 1e-8 else .5
                      for axis, (lo, hi) in zip(axes, bounds)]
            # +X driver side sees forward at screen-left; flip its exact text
            # receiver so POLICE reads normally from outside.
            if material == "POLICE_DRIVER":
                values[0] = 1 - values[0]
            coordinates.append(values)
        for loop_index, values in zip(polygon.loop_indices, coordinates):
            uv.data[loop_index].uv = (
                (x0 + 2 + values[0] * (x1 - x0 - 4)) / ATLAS_SIZE,
                1 - (y1 - 2 - values[1] * (y1 - y0 - 4)) / ATLAS_SIZE,
            )


def clean_and_map(objects):
    for obj in objects:
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-7)
        bmesh.ops.dissolve_degenerate(bm, edges=list(bm.edges), dist=1e-8)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()
        map_object(obj)


def add_wheel_anchors():
    for side, side_name in ((-1, "L"), (1, "R")):
        for axle, axle_name in ((WHEELS["front_z"], "F"),
                                (WHEELS["rear_z"], "R")):
            empty = bpy.data.objects.new("WHEEL_" + axle_name + side_name, None)
            empty.empty_display_type = "SPHERE"
            empty.empty_display_size = .10
            empty.location = (side * WHEELS["x"], axle, WHEELS["arch_y"])
            bpy.context.collection.objects.link(empty)


def build_vehicle(articulated=False):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for material in list(bpy.data.materials):
        bpy.data.materials.remove(material)
    builder = VehicleBuilder()
    fixed = []
    door_parts = []
    glass = {}

    # Side shells are split at real door boundaries.  The moving +X segment is
    # absent from body_open; its low sill stays with the chassis.
    for side in (-1, 1):
        side_shell(builder, "RearQuarterShell" + str(side), side,
                   arch_stations(-2.58, REAR_DOOR_REAR, WHEELS["rear_z"]), fixed)
        side_shell(builder, "RearDoorShell" + str(side), side,
                   [(REAR_DOOR_REAR, .20), (DRIVER_DOOR_REAR, .20)], fixed)
        if side < 0:
            side_shell(builder, "PassengerFrontDoorShell", side,
                       [(DRIVER_DOOR_REAR, .20), (DRIVER_DOOR_FRONT, .20)], fixed)
        else:
            side_shell(builder, "DriverDoorSill", side,
                       [(DRIVER_DOOR_REAR, .20), (DRIVER_DOOR_FRONT, .20)],
                       fixed, forced_top=.40)
            door = side_prism(builder, "DriverFrontDoor", side,
                              [(DRIVER_DOOR_REAR, .38),
                               (DRIVER_DOOR_FRONT, .38),
                               (DRIVER_DOOR_FRONT, 1.02),
                               (DRIVER_DOOR_REAR, 1.02)], .046, "BODY_SIDE",
                              taper=False)
            door_parts.append(door)
        side_shell(builder, "FrontQuarterShell" + str(side), side,
                   arch_stations(DRIVER_DOOR_FRONT, 2.58, WHEELS["front_z"]), fixed)

    tracked_box(builder, fixed, "CentralChassis", (-.49, -2.56, .20),
                (.49, 2.56, .34), "NAVY")
    top_loft(builder, fixed, "SlopedHood", [
        (.82, .72, .965, .995), (1.25, .74, .955, .985),
        (1.63, .74, .925, .955), (2.10, .70, .865, .895),
        (2.58, .60, .775, .805),
    ])
    top_loft(builder, fixed, "ClippedFormalTrunk", [
        (-2.58, .64, .825, .845), (-2.25, .72, .885, .910),
        (-1.75, .74, .925, .950), (-1.30, .72, .965, .985),
    ])
    tracked_box(builder, fixed, "FrontLowerReceiver", (-.72, 2.54, .34),
                (.72, 2.58, .78), "BODY_FRONT")
    tracked_box(builder, fixed, "RearLowerReceiver", (-.72, -2.58, .34),
                (.72, -2.54, .84), "BODY_REAR")
    tracked_box(builder, fixed, "TrunkLip", (-.72, -2.59, .83),
                (.72, -2.54, .90), "BODY_TOP")

    make_interior(builder, fixed)
    make_cabin(builder, fixed, door_parts, glass)
    make_exterior_details(builder, fixed, door_parts)

    all_objects = fixed + door_parts + [glass[name] for name in GLASS_NAMES]
    if len(set(all_objects)) != len(all_objects) or set(all_objects) != set(builder.objects):
        missing = set(builder.objects) - set(all_objects)
        duplicated = len(all_objects) - len(set(all_objects))
        raise AssertionError(("component ownership mismatch", missing, duplicated))
    clean_and_map(all_objects)
    add_wheel_anchors()

    if not articulated:
        builder.objects = all_objects
        body = builder.join()
        body.name = "BODY"
        body.data.name = "MunicipalCruiser91BClosedBody"
        return body

    builder.objects = fixed
    body_open = builder.join()
    body_open.name = "BODY_OPEN"
    body_open.data.name = "MunicipalCruiser91BOpenBody"
    builder.objects = door_parts
    driver_door = builder.join()
    driver_door.name = "DRIVER_DOOR"
    driver_door.data.name = "MunicipalCruiser91BDriverDoor"
    for name, pane in glass.items():
        pane.name = name
        pane.data.name = "MunicipalCruiser91B_" + name
    assert len([obj for obj in bpy.context.scene.objects if obj.type == "MESH"]) == 8
    return body_open, driver_door, glass


def configure_materials(objects, texture_path, blend_path):
    atlas = bpy.data.images.load(str(texture_path), check_existing=True)
    atlas.filepath = bpy.path.relpath(str(texture_path), start=str(blend_path.parent))
    for obj in objects:
        for slot in obj.material_slots:
            material = slot.material
            material.use_nodes = True
            shader = material.node_tree.nodes.get("Principled BSDF")
            shader.inputs["Roughness"].default_value = 1.0
            if material.name.startswith("GLASS_"):
                shader.inputs["Alpha"].default_value = .42
                material.surface_render_method = "DITHERED"
            texture = material.node_tree.nodes.new("ShaderNodeTexImage")
            texture.image = atlas
            texture.interpolation = "Closest"
            material.node_tree.links.new(texture.outputs["Color"],
                                         shader.inputs["Base Color"])


def main():
    source = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesh", type=Path, required=True)
    parser.add_argument("--blend", type=Path, required=True)
    args = parser.parse_args(source)
    texture_path = (Path(__file__).resolve().parents[1] /
                    "assets/textures/vehicles/municipal_cruiser_91b/body.png")
    args.mesh.parent.mkdir(parents=True, exist_ok=True)
    args.blend.parent.mkdir(parents=True, exist_ok=True)

    body = build_vehicle(False)
    configure_materials([body], texture_path, args.blend)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print("MUNICIPAL_CRUISER_91B_CLOSED",
          export_emesh(body, args.mesh, "municipal_cruiser_91b"))

    body_open, driver_door, glass = build_vehicle(True)
    articulated_path = args.blend.with_name("articulated.blend")
    configure_materials([body_open, driver_door, *glass.values()],
                        texture_path, articulated_path)
    bpy.ops.wm.save_as_mainfile(filepath=str(articulated_path))
    print("MUNICIPAL_CRUISER_91B_OPEN",
          export_emesh(body_open, args.mesh.with_name("body_open.emesh"),
                       "municipal_cruiser_91b"))
    print("MUNICIPAL_CRUISER_91B_DOOR",
          export_emesh(driver_door, args.mesh.with_name("driver_door.emesh"),
                       "municipal_cruiser_91b"))
    for name in GLASS_NAMES:
        print("MUNICIPAL_CRUISER_91B_GLASS", name,
              export_emesh(glass[name], args.mesh.with_name(name + ".emesh"),
                           "municipal_cruiser_91b"))


if __name__ == "__main__":
    main()
