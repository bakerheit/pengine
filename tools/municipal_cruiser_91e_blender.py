#!/usr/bin/env python3
"""Deterministic long-hood 1991 highway police sedan with opening door."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import municipal_cruiser_91a_blender as base  # noqa: E402
from municipal_cruiser_91e_spec import (  # noqa: E402
    ATLAS_SIZE, DOOR, PANE_FILES, REGIONS, SHAPE, UV_PROJECTIONS, WHEELS,
)
from vesper_vx91_blender import export_emesh  # noqa: E402

# Reuse the proven cooker helpers while binding them to this attempt's frozen
# semantic receivers. Geometry below is authored independently for 91E.
base.ATLAS_SIZE = ATLAS_SIZE
base.DOOR = DOOR
base.PANE_FILES = PANE_FILES
base.REGIONS = REGIONS
base.SHAPE = SHAPE
base.UV_PROJECTIONS = UV_PROJECTIONS
base.WHEELS = WHEELS

CruiserBuilder = base.CruiserBuilder
beam = base.beam
closed_pane = base.closed_pane
add_material = base.add_material
side_card = base.side_card
clean_object = base.clean_object
join_objects = base.join_objects


def profile(y: float) -> tuple[float, float]:
    """Broad shoulder and low formal hood/deck at each longitudinal station."""
    keys = (
        (-2.780, .98, .78), (-2.62, 1.04, .90), (-2.18, 1.08, .97),
        (-1.60, 1.08, 1.00), (-1.06, 1.075, .98), (.78, 1.075, .98),
        (1.12, 1.08, .97), (1.72, 1.08, .99), (2.24, 1.075, .96),
        (2.62, 1.04, .90), (2.780, .98, .78),
    )
    for left, right in zip(keys, keys[1:]):
        if left[0] - 1e-7 <= y <= right[0] + 1e-7:
            t = (y - left[0]) / (right[0] - left[0])
            return tuple(left[i] + t * (right[i] - left[i]) for i in (1, 2))
    raise ValueError(y)


def body_stations() -> list[tuple[float, float]]:
    r = SHAPE["arch_long_radius"]
    h = SHAPE["arch_height_radius"]
    c = WHEELS["arch_y"]
    result = [(-2.780, .18), (-2.43, .18)]
    for axle in (WHEELS["rear_z"], WHEELS["front_z"]):
        result.extend(((axle - r, .18), (axle - r, c)))
        result.extend((axle - r * math.cos(i * math.pi / 7),
                       c + h * math.sin(i * math.pi / 7))
                      for i in range(1, 7))
        result.extend(((axle + r, c), (axle + r, .18)))
        if axle < 0:
            result.extend(((-.76, .18), (DOOR["rear_z"], .18),
                           (DOOR["front_z"], .18)))
    result.extend(((2.45, .18), (2.780, .18)))
    return sorted(set(result), key=lambda item: item[0])


def build_side_shell(builder: CruiserBuilder, side: float):
    rings = []
    for y, low in body_stations():
        width, top = profile(y)
        points = ((.61, low), (width, low + .035),
                  (width, top - .13), (.96 * width, top), (.66, top - .012))
        rings.append((y, [(side * x, z) for x, z in points]))
    shell = builder.loft("HighwayShoulder" + ("L" if side < 0 else "R"),
                         rings, "BODY_SIDE")
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
        doomed = []
        for face in bm.faces:
            centre = face.calc_center_median()
            if (DOOR["rear_z"] + 1e-5 < centre.y < DOOR["front_z"] - 1e-5
                    and centre.z > DOOR["sill_y"] - .015 and centre.x > .59):
                doomed.append(face)
        bmesh.ops.delete(bm, geom=doomed, context="FACES")
        bm.to_mesh(shell.data)
        bm.free()
        shell.data.update()
    return shell


def add_interior(builder: CruiserBuilder):
    parts = [
        builder.box("CabinFloor", (-.78, -1.29, .34), (.78, .76, .40),
                    "INTERIOR"),
        builder.box("TransmissionTunnel", (-.14, -1.14, .40),
                    (.14, .63, .54), "INTERIOR"),
        builder.box("Firewall", (-.79, .74, .40), (.79, .80, .98), "SHADOW"),
        builder.box("RearBulkhead", (-.79, -1.31, .40),
                    (.79, -1.25, .98), "SHADOW"),
    ]
    for row, y in (("Front", .14), ("Rear", -.79)):
        for suffix, x in (("L", -.44), ("R", .44)):
            parts.append(builder.box(row + "SeatCushion" + suffix,
                                     (x - .24, y - .25, .54),
                                     (x + .24, y + .23, .70), "INTERIOR"))
            parts.append(builder.box(row + "SeatBack" + suffix,
                                     (x - .24, y - .29, .68),
                                     (x + .24, y - .21, 1.18), "INTERIOR"))
    parts.append(builder.loft("Dashboard", [
        (.52, [(-.76, .88), (-.76, 1.09), (.76, 1.09), (.76, .88)]),
        (.73, [(-.76, .86), (-.76, .99), (.76, .99), (.76, .86)]),
    ], "INTERIOR"))
    parts.append(builder.box("GaugeBinnacle", (.20, .50, 1.04),
                             (.68, .67, 1.15), "BLACK"))
    parts.append(base.steering_rim(builder))
    parts.append(builder.box("SteeringColumn", (.41, .55, 1.00),
                             (.47, .73, 1.07), "METAL"))
    for x in (.34, .54):
        parts.append(builder.box("Pedal" + str(x), (x - .035, .62, .405),
                                 (x + .035, .71, .445), "METAL"))
    parts.append(builder.panel("RearViewMirror", [(-.15, .30, 1.34),
                                                   (.15, .30, 1.34),
                                                   (.15, .30, 1.43),
                                                   (-.15, .30, 1.43)],
                               "BLACK", (0, 1, 0)))
    return parts


def add_greenhouse(builder: CruiserBuilder, door_parts: list):
    body_parts = []
    roof_ring = [(-.80, 1.45), (-.75, 1.50), (.75, 1.50), (.80, 1.45)]
    body_parts.append(builder.loft("LowFormalRoof", [(-.99, roof_ring),
                                                       (.28, roof_ring)],
                                   "DOOR_WHITE"))
    for side in (-1., 1.):
        suffix = "L" if side < 0 else "R"
        a_low = (side * 1.015, .78, .99)
        a_top = (side * .80, .28, 1.47)
        b_low = (side * 1.015, -.24, .99)
        b_top = (side * .80, -.24, 1.47)
        c_low = (side * 1.015, -1.31, .99)
        c_top = (side * .80, -.99, 1.47)
        for label, start, end in (
            ("RakedAPillar", a_low, a_top), ("BPillar", b_low, b_top),
            ("BroadCPillar", c_low, c_top), ("RoofRailFront", a_top, b_top),
            ("RoofRailRear", b_top, c_top), ("WindowSillFront", a_low, b_low),
            ("WindowSillRear", b_low, c_low),
        ):
            body_parts.append(beam(builder, label + suffix, start, end, .065,
                                   "DOOR_WHITE" if "Roof" in label else "BODY_SIDE"))
    for label, start, end in (
        ("WindshieldHeader", (-.80, .28, 1.47), (.80, .28, 1.47)),
        ("WindshieldCowl", (-1.015, .78, .99), (1.015, .78, .99)),
        ("RearHeader", (-.80, -.99, 1.47), (.80, -.99, 1.47)),
        ("RearShelfRail", (-1.015, -1.31, .99), (1.015, -1.31, .99)),
    ):
        body_parts.append(beam(builder, label, start, end, .066, "BODY_SIDE"))

    door_parts.append(builder.box("DriverDoorShell", (1.00, -.24, .47),
                                  (1.08, .78, .99), "DOOR_WHITE"))
    edges = (
        ((1.015, .74, 1.00), (.81, .30, 1.46)),
        ((1.015, -.20, 1.00), (.81, -.20, 1.46)),
        ((1.015, .74, 1.00), (1.015, -.20, 1.00)),
        ((.81, .30, 1.46), (.81, -.20, 1.46)),
    )
    for index, (start, end) in enumerate(edges):
        door_parts.append(beam(builder, "DriverDoorFrame" + str(index),
                               start, end, .052, "DOOR_WHITE"))
    door_parts.append(side_card(builder, "DriverCreamPanel", 1,
                                [(-.21, .51), (.75, .51), (.75, .97),
                                 (-.21, .97)], "POLICE", 1.081))
    door_parts.append(builder.box("DriverHandle", (1.058, -.15, .94),
                                  (1.08, .05, .99), "METAL"))
    door_parts.append(builder.box("DriverMirror", (1.00, .52, .99),
                                  (1.08, .73, 1.13), "RUBBER"))
    return body_parts


def add_static_doors(builder: CruiserBuilder):
    parts = []
    for side in (-1., 1.):
        suffix = "L" if side < 0 else "R"
        if side < 0:
            parts.append(side_card(builder, "PassengerFrontDoor", side,
                                   [(-.24, .47), (.78, .47), (.78, .99),
                                    (-.24, .99)], "DOOR_WHITE", 1.081))
            parts.append(side_card(builder, "PassengerCreamPanel", side,
                                   [(-.21, .51), (.75, .51), (.75, .97),
                                    (-.21, .97)], "POLICE", 1.083))
            parts.append(builder.box("PassengerMirror", (-1.08, .52, .99),
                                     (-1.00, .73, 1.13), "RUBBER"))
        parts.append(side_card(builder, "RearDoor" + suffix, side,
                               [(-1.31, .47), (-.24, .47), (-.24, .99),
                                (-1.31, .99)], "DOOR_WHITE", 1.081))
        # A long cream patrol stripe ties front and rear doors together.
        parts.append(side_card(builder, "RearCreamPanel" + suffix, side,
                               [(-1.28, .51), (-.27, .51), (-.27, .97),
                                (-1.28, .97)], "DOOR_WHITE", 1.083))
        for label, y in (("Rear", -1.18), *(([("Front", -.15)]) if side < 0 else [])):
            parts.append(side_card(builder, label + "Handle" + suffix, side,
                                   [(y, .94), (y + .19, .94),
                                    (y + .19, .99), (y, .99)], "METAL", 1.086))
        parts.append(side_card(builder, "BeltRub" + suffix, side,
                               [(-2.35, .46), (2.30, .46), (2.30, .51),
                                (-2.35, .51)], "RUBBER", 1.086))
    return parts


def side_x(side: float, height: float) -> float:
    return side * (1.026 - (height - .99) * (.215 / .48))


def add_panes(builder: CruiserBuilder):
    panes = {}
    panes["windshield"] = closed_pane(builder, "windshield", [
        (-.93, .75, 1.035), (.93, .75, 1.035),
        (.745, .305, 1.455), (-.745, .305, 1.455),
    ], (0, 1, .5))
    panes["rear_glass"] = closed_pane(builder, "rear_glass", [
        (.745, -.995, 1.455), (-.745, -.995, 1.455),
        (-.93, -1.285, 1.035), (.93, -1.285, 1.035),
    ], (0, -1, .45))
    front_yz = ((.735, 1.035), (-.195, 1.035),
                (-.195, 1.445), (.315, 1.445))
    rear_yz = ((-.285, 1.035), (-1.265, 1.035),
               (-.975, 1.445), (-.285, 1.445))
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


def faceted_bumper(builder: CruiserBuilder, name: str, front: bool):
    y0, y1 = ((2.67, 2.78) if front else (-2.78, -2.67))
    ring = [(-.98, .25), (-1.08, .31), (-1.08, .44), (-.98, .50),
            (.98, .50), (1.08, .44), (1.08, .31), (.98, .25)]
    return builder.loft(name, [(y0, ring), (y1, ring)], "METAL")


def add_exterior_details(builder: CruiserBuilder):
    parts = [faceted_bumper(builder, "FrontBumper", True),
             faceted_bumper(builder, "RearBumper", False),
             # Keep the broad fascia one millimetre behind the semantic lamp
             # cards. Coplanar receiver triangles can interpolate just past
             # 2.780 on one side and make the exact runtime lens profile miss.
             builder.box("FrontReceiver", (-.99, 2.70, .45),
                         (.99, 2.779, .88), "FRONT"),
             builder.box("RearReceiver", (-.99, -2.779, .46),
                         (.99, -2.70, .88), "REAR")]
    for side in (-1., 1.):
        for index, (inner, outer) in enumerate(((.12, .44), (.50, .88))):
            x0, x1 = sorted((side * inner, side * outer))
            parts.append(builder.panel(f"InsetLamp{side}{index}",
                                       [(x0, 2.78, .57), (x1, 2.78, .57),
                                        (x1, 2.78, .76), (x0, 2.78, .76)],
                                       "HEADLIGHT", (0, 1, 0)))
        x0, x1 = sorted((side * .73, side * .95))
        parts.append(builder.panel("FrontIndicator" + str(side),
                                   [(x0, 2.78, .47), (x1, 2.78, .47),
                                    (x1, 2.78, .55), (x0, 2.78, .55)],
                                   "AMBER", (0, 1, 0)))
        x0, x1 = sorted((side * .55, side * .95))
        parts.append(builder.panel("RearTail" + str(side),
                                   [(x1, -2.78, .58), (x0, -2.78, .58),
                                    (x0, -2.78, .80), (x1, -2.78, .80)],
                                   "TAIL_RED", (0, -1, 0)))
        x0, x1 = sorted((side * .36, side * .52))
        parts.append(builder.panel("RearReverse" + str(side),
                                   [(x1, -2.78, .58), (x0, -2.78, .58),
                                    (x0, -2.78, .80), (x1, -2.78, .80)],
                                   "REVERSE", (0, -1, 0)))
    for x in (-.40, -.30, -.20, -.10, 0, .10, .20, .30, .40):
        parts.append(builder.panel("GrilleBar" + str(x),
                                   [(x - .014, 2.78, .48),
                                    (x + .014, 2.78, .48),
                                    (x + .014, 2.78, .83),
                                    (x - .014, 2.78, .83)],
                                   "METAL", (0, 1, 0)))

    # Deep wraparound push bumper, including corner wings visible in profile.
    for x in (-.53, .53):
        parts.append(builder.box("PushUpright" + str(x),
                                 (x - .045, 2.70, .31),
                                 (x + .045, 2.78, .91), "RUBBER"))
    for z in (.48, .78):
        parts.append(builder.box("PushCross" + str(z),
                                 (-.72, 2.715, z - .045),
                                 (.72, 2.78, z + .045), "RUBBER"))
    for side in (-1., 1.):
        x0, x1 = sorted((side * .70, side * 1.075))
        parts.append(builder.box("PushWing" + str(side),
                                 (x0, 2.64, .43), (x1, 2.72, .53), "RUBBER"))

    for x in (-.48, .48):
        parts.append(builder.box("LightbarFoot" + str(x),
                                 (x - .045, -.18, 1.50),
                                 (x + .045, -.05, 1.53), "RUBBER"))
    parts.append(builder.box("LightbarBase", (-.80, -.30, 1.52),
                             (.80, .04, 1.56), "RUBBER"))
    parts.append(builder.box("LightbarRed", (-.76, -.28, 1.56),
                             (-.05, .02, 1.70), "LIGHTBAR_RED"))
    parts.append(builder.box("LightbarBlue", (.05, -.28, 1.56),
                             (.76, .02, 1.70), "LIGHTBAR_BLUE"))
    parts.append(builder.box("LightbarSpeaker", (-.045, -.27, 1.56),
                             (.045, .01, 1.68), "BLACK"))

    for side in (-1., 1.):
        parts.append(builder.cylinder("Spotlight" + str(side),
                                     (side * .94, .58, 1.30), .105, .08, 8,
                                     "METAL"))
        parts.append(builder.cylinder("SpotlightLens" + str(side),
                                     (side * .94, .625, 1.30), .082, .012, 8,
                                     "HEADLIGHT"))
        parts.append(beam(builder, "SpotlightArm" + str(side),
                          (side * .91, .52, 1.20),
                          (side * .94, .57, 1.28), .034, "METAL"))
        # Twin trunk-mounted highway whip antennae stay below lightbar height.
        parts.append(builder.cylinder("AntennaBase" + str(side),
                                     (side * .48, -1.93, 1.00), .045, .08, 8,
                                     "RUBBER"))
        parts.append(beam(builder, "WhipAntenna" + str(side),
                          (side * .48, -1.95, 1.02),
                          (side * .43, -2.04, 1.65), .014, "METAL"))
    return parts


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
    body_parts.append(builder.box("CentralChassis", (-.48, -2.70, .18),
                                  (.48, 2.70, .35), "SHADOW"))
    for name, low, high in (("LongInterceptorHood", .78, 2.78),
                            ("LongFormalDeck", -2.78, -1.31)):
        sections = []
        ys = [low, high]
        ys.extend(axle for axle in (WHEELS["rear_z"], WHEELS["front_z"])
                  if low < axle < high)
        ys.extend(station for station in (-2.18, 2.24) if low < station < high)
        for y in sorted(set(ys)):
            top = profile(y)[1]
            sections.append((y, [(-.68, top - .035), (-.68, top),
                                 (.68, top), (.68, top - .035)]))
        body_parts.append(builder.loft(name, sections, "DOOR_WHITE"))
    # Two subtle hood planes make the interceptor read broad without a blob.
    body_parts.append(builder.panel("HoodPowerRidgeL", [
        (-.43, .83, .986), (-.19, .83, .995),
        (-.19, 2.38, .925), (-.43, 2.38, .916)], "BODY_TOP", (0, 0, 1)))
    body_parts.append(builder.panel("HoodPowerRidgeR", [
        (.19, .83, .995), (.43, .83, .986),
        (.43, 2.38, .916), (.19, 2.38, .925)], "BODY_TOP", (0, 0, 1)))
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
        closed = join_objects(builder.objects, "BODY", "MunicipalCruiser91EBody")
        assert len([obj for obj in bpy.context.scene.objects if obj.type == "MESH"]) == 1
        return closed
    body_open = join_objects(body_objects, "BODY_OPEN",
                             "MunicipalCruiser91EOpenBody")
    driver_door = join_objects(door_parts, "DRIVER_DOOR",
                               "MunicipalCruiser91EDriverDoor")
    assert set(panes) == set(PANE_FILES)
    assert len([obj for obj in bpy.context.scene.objects if obj.type == "MESH"]) == 8
    return body_open, driver_door, panes


def configure_materials(objects, texture_path: Path):
    return base.configure_materials(objects, texture_path)


def main():
    source = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--mesh", type=Path, required=True)
    parser.add_argument("--blend", type=Path, required=True)
    args = parser.parse_args(source)
    root = Path(__file__).resolve().parents[1]
    texture = root / "assets/textures/vehicles/municipal_cruiser_91e/body.png"
    args.mesh.parent.mkdir(parents=True, exist_ok=True)

    closed = build_vehicle(False)
    configure_materials([closed], texture)
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print("CRUISER_91E_CLOSED", export_emesh(closed, args.mesh,
                                              "municipal_cruiser_91e"))

    body_open, driver_door, panes = build_vehicle(True)
    configure_materials([body_open, driver_door, *panes.values()], texture)
    print("CRUISER_91E_OPEN", export_emesh(
        body_open, args.mesh.with_name("body_open.emesh"), "municipal_cruiser_91e"))
    print("CRUISER_91E_DOOR", export_emesh(
        driver_door, args.mesh.with_name("driver_door.emesh"), "municipal_cruiser_91e"))
    for name in PANE_FILES:
        print("CRUISER_91E_PANE", name, export_emesh(
            panes[name], args.mesh.with_name(name + ".emesh"),
            "municipal_cruiser_91e"))
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend.with_name("articulated.blend")))


if __name__ == "__main__":
    main()
