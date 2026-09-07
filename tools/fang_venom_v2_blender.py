#!/usr/bin/env python3
"""Build the detailed second Fang Venom body and distinct wheel assemblies."""

import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fang_venom_v2_spec import ATLAS_SIZE, REGIONS, WHEELS
from vesper_vx91_blender import VehicleBuilder, export_emesh


DETAIL_MATERIALS = {
    "FRAME", "ENGINE", "METAL", "BLACK", "SEAT", "TIRE", "RIM",
    "BRAKE", "HEADLIGHT", "TAIL_RED", "AMBER", "GAUGE", "EXHAUST",
    "DECAL", "CHAIN", "RUBBER", "PLATE",
}


def region_name(material):
    # Each component owns a small builder. Blender suffixes reused global
    # material names with .001/.002, but they still map to the same atlas cell.
    return material.name.split(".", 1)[0]


def beam(builder, name, start, end, width, material):
    """A square tube between two measured source-space points."""
    direction = (Vector(end) - Vector(start)).normalized()
    reference = Vector((0, 0, 1)) if abs(direction.z) < .92 else Vector((0, 1, 0))
    side = direction.cross(reference).normalized() * width * .5
    up = direction.cross(side).normalized() * width * .5
    points = [tuple(Vector(point) + a * side + b * up)
              for point in (start, end)
              for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    return builder.add_mesh(name, points, faces, material)


def x_cylinder(builder, name, centre, radius, depth, sides, material):
    """Cylinder on the bike's lateral X axle."""
    cx, cy, cz = centre
    vertices = []
    for x in (cx - depth * .5, cx + depth * .5):
        for index in range(sides):
            angle = math.tau * index / sides
            vertices.append((x, cy + math.sin(angle) * radius,
                             cz + math.cos(angle) * radius))
    faces = []
    for index in range(sides):
        following = (index + 1) % sides
        faces.append((index, following, sides + following, sides + index))
    faces.append(tuple(reversed(range(sides))))
    faces.append(tuple(sides + index for index in range(sides)))
    return builder.add_mesh(name, vertices, faces, material)


def torus_x(builder, name, major, minor, width_offset, around, cross, material):
    vertices = []
    for index in range(around):
        theta = math.tau * index / around
        for section in range(cross):
            phi = math.tau * section / cross
            radius = major + minor * math.cos(phi)
            vertices.append((width_offset + minor * math.sin(phi),
                             radius * math.sin(theta),
                             radius * math.cos(theta)))
    faces = []
    for index in range(around):
        for section in range(cross):
            a = index * cross + section
            b = ((index + 1) % around) * cross + section
            faces.append((a, b,
                          ((index + 1) % around) * cross + (section + 1) % cross,
                          index * cross + (section + 1) % cross))
    return builder.add_mesh(name, vertices, faces, material)


def panel_prism(builder, name, side, points_yz, inner, outer, material):
    """Thin shaped fairing panel with readable edge thickness."""
    xs = sorted((side * inner, side * outer))
    vertices = [(x, y, z) for x in xs for y, z in points_yz]
    count = len(points_yz)
    faces = [tuple(reversed(range(count))), tuple(count + i for i in range(count))]
    for index in range(count):
        following = (index + 1) % count
        faces.append((index, following, count + following, count + index))
    return builder.add_mesh(name, vertices, faces, material)


def fender(builder, name, axle_y, radius, width, begin, end, material, segments=10):
    vertices = []
    inner = radius
    outer = radius + .045
    for index in range(segments + 1):
        angle = begin + (end - begin) * index / segments
        for x, r in ((-width * .5, inner), (width * .5, inner),
                     (-width * .5, outer), (width * .5, outer)):
            vertices.append((x, axle_y + math.cos(angle) * r,
                             WHEELS["centre_y"] + math.sin(angle) * r))
    faces = []
    for index in range(segments):
        a, b = index * 4, (index + 1) * 4
        faces.extend(((a, b, b + 1, a + 1), (a + 2, a + 3, b + 3, b + 2),
                      (a, a + 2, b + 2, b), (a + 1, b + 1, b + 3, a + 3)))
    faces.extend(((0, 1, 3, 2),
                  (segments * 4, segments * 4 + 2,
                   segments * 4 + 3, segments * 4 + 1)))
    return builder.add_mesh(name, vertices, faces, material)


def fairing_ring(width, bottom, shoulder, top):
    return [(-width * .62, bottom), (-width, bottom + .055),
            (-width, shoulder), (-width * .55, top),
            (width * .55, top), (width, shoulder),
            (width, bottom + .055), (width * .62, bottom)]


def material_face(builder, obj, face, name):
    material = builder.material(name)
    if material.name not in obj.data.materials:
        obj.data.materials.append(material)
    face.material_index = obj.data.materials.find(material.name)


def content_box(name):
    x0, y0, x1, y1 = REGIONS[name]
    return x0 + 3, y0 + 12, x1 - 3, y1 - 3


def map_uvs(obj):
    mesh = obj.data
    if mesh.uv_layers:
        mesh.uv_layers.remove(mesh.uv_layers[0])
    uv = mesh.uv_layers.new(name="UVMap")

    def axes_for(face):
        normal = face.normal
        if abs(normal.z) >= max(abs(normal.x), abs(normal.y)):
            return 0, 1
        if abs(normal.y) >= abs(normal.x):
            return 0, 2
        return 1, 2

    projected = {}
    for face in mesh.polygons:
        name = region_name(mesh.materials[face.material_index])
        if name in DETAIL_MATERIALS:
            continue
        axes = axes_for(face)
        points = [mesh.vertices[mesh.loops[i].vertex_index].co for i in face.loop_indices]
        bounds = (min(p[axes[0]] for p in points), max(p[axes[0]] for p in points),
                  min(p[axes[1]] for p in points), max(p[axes[1]] for p in points))
        key = name, axes
        previous = projected.get(key)
        projected[key] = bounds if previous is None else (
            min(previous[0], bounds[0]), max(previous[1], bounds[1]),
            min(previous[2], bounds[2]), max(previous[3], bounds[3]))

    for face in mesh.polygons:
        name = region_name(mesh.materials[face.material_index])
        axes = axes_for(face)
        points = [mesh.vertices[mesh.loops[i].vertex_index].co for i in face.loop_indices]
        if name in DETAIL_MATERIALS:
            bounds = (min(p[axes[0]] for p in points), max(p[axes[0]] for p in points),
                      min(p[axes[1]] for p in points), max(p[axes[1]] for p in points))
        else:
            bounds = projected[name, axes]
        a_span, b_span = bounds[1] - bounds[0], bounds[3] - bounds[2]
        x0, y0, x1, y1 = content_box(name)
        for loop_index, point in zip(face.loop_indices, points):
            a = .5 if a_span < 1e-6 else (point[axes[0]] - bounds[0]) / a_span
            b = .5 if b_span < 1e-6 else (point[axes[1]] - bounds[2]) / b_span
            uv.data[loop_index].uv = ((x0 + a * (x1 - x0)) / ATLAS_SIZE,
                                      1 - (y1 - b * (y1 - y0)) / ATLAS_SIZE)


def clean_and_join(builder, name):
    for obj in builder.objects:
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-6)
        bmesh.ops.triangulate(bm, faces=list(bm.faces))
        bmesh.ops.dissolve_degenerate(bm, edges=list(bm.edges), dist=1e-7)
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()
    # Blender warns when join() is asked to merge a one-object group.  The
    # closed windscreen is intentionally one mesh, so keep it directly.
    joined = builder.objects[0] if len(builder.objects) == 1 else builder.join()
    joined.name = name
    joined.data.name = "FangVenomV2" + name.title().replace("_", "")
    map_uvs(joined)
    return joined


def build_body():
    b = VehicleBuilder()

    # Compact twin-spar frame and lower cradle remain visible through the side
    # fairing openings.  These tubes define the structure rather than acting as
    # painted-on decoration.
    b.box("Spine", (-.095, -.67, .39), (.095, .56, .49), "BLACK")
    for side in (-1, 1):
        x = side * .19
        suffix = "L" if side < 0 else "R"
        beam(b, "SparUpper" + suffix, (x, -.48, .68), (x, .40, .93), .052, "FRAME")
        beam(b, "SparLower" + suffix, (x, -.48, .68), (x, .34, .48), .046, "FRAME")
        beam(b, "Cradle" + suffix, (x, .30, .49), (x, -.42, .39), .040, "FRAME")
        beam(b, "SwingArmTop" + suffix, (side * .16, -.17, .56),
             (side * .105, -.72, .37), .062, "METAL")
        beam(b, "SwingArmLower" + suffix, (side * .16, -.18, .46),
             (side * .105, -.72, .33), .052, "METAL")

    # Transverse four-cylinder motor, cooling fins, side covers and radiator.
    b.box("Crankcase", (-.255, -.31, .43), (.255, .23, .66), "ENGINE")
    b.box("CylinderBank", (-.265, -.02, .63), (.265, .27, .79), "ENGINE")
    for index, z in enumerate((.48, .525, .57, .615, .665, .708, .75)):
        b.box("CoolingFin" + str(index), (-.29, -.285, z), (.29, .265, z + .015), "METAL")
    for side in (-1, 1):
        x_cylinder(b, "EngineCover" + str(side), (side * .275, -.06, .55),
                   .135, .034, 12, "ENGINE")
    b.box("Radiator", (-.255, .305, .53), (.255, .365, .77), "BLACK")
    for index in range(7):
        z = .545 + index * .031
        b.box("RadiatorFin" + str(index), (-.235, .366, z), (.235, .378, z + .012), "METAL")

    # Connected belly pan, wedge nose and tank use more longitudinal stations
    # than attempt one.  The necks between them are intentional, not stacked
    # cubes, and neither wheel opening is filled by body geometry.
    belly = b.loft("BellyPan", [
        (-.48, fairing_ring(.19, .35, .50, .59)),
        (-.18, fairing_ring(.27, .34, .58, .70)),
        (.22, fairing_ring(.29, .35, .64, .78)),
        (.49, fairing_ring(.24, .39, .68, .80)),
    ], "PAINT_LOWER")
    for face in belly.data.polygons:
        if face.normal.z > .62:
            material_face(b, belly, face, "PAINT_TOP")

    tank = b.loft("FuelTank", [
        (-.39, fairing_ring(.18, .67, .79, .91)),
        (-.22, fairing_ring(.25, .68, .88, 1.075)),
        (.02, fairing_ring(.285, .69, .91, 1.135)),
        (.25, fairing_ring(.25, .69, .88, 1.09)),
        (.39, fairing_ring(.17, .70, .82, .99)),
    ], "PAINT_TOP")
    for face in tank.data.polygons:
        if abs(face.normal.x) > .42:
            material_face(b, tank, face, "PAINT_SIDE")

    nose = b.loft("NoseCowl", [
        (.40, fairing_ring(.22, .51, .79, 1.00)),
        (.58, fairing_ring(.285, .52, .83, 1.055)),
        (.82, fairing_ring(.275, .57, .87, 1.065)),
        (1.04, fairing_ring(.225, .66, .90, 1.035)),
        (1.12, fairing_ring(.175, .76, .91, .985)),
    ], "PAINT_SIDE")
    for face in nose.data.polygons:
        if face.normal.z > .56:
            material_face(b, nose, face, "PAINT_TOP")
        elif face.normal.z < -.45:
            material_face(b, nose, face, "PAINT_LOWER")

    # Shaped side fairings and actual inset vent cavities.
    for side in (-1, 1):
        suffix = "L" if side < 0 else "R"
        panel_prism(b, "MidFairing" + suffix, side,
                    [(-.34, .46), (.05, .40), (.47, .52), (.54, .76),
                     (.22, .82), (-.19, .72)], .245, .305, "PAINT_SIDE")
        panel_prism(b, "VentWell" + suffix, side,
                    [(.06, .53), (.36, .57), (.39, .69), (.13, .68)],
                    .306, .313, "BLACK")
        panel_prism(b, "VenomGraphic" + suffix, side,
                    [(-.28, .645), (-.04, .708), (.10, .706), (-.13, .65)],
                    .314, .318, "DECAL")
        for slat in range(3):
            z = .565 + slat * .045
            beam(b, "VentSlat" + suffix + str(slat),
                 (side * .318, .10, z), (side * .318, .35, z + .025), .018, "METAL")

    # Seat and tail preserve a slim waist with daylight underneath.
    b.loft("RiderSeat", [
        (-.67, [(-.17, .79), (-.22, .84), (-.20, .895), (.20, .895),
                 (.22, .84), (.17, .79)]),
        (-.23, [(-.20, .79), (-.225, .84), (-.205, .91), (.205, .91),
                 (.225, .84), (.20, .79)]),
    ], "SEAT")
    tail = b.loft("TailCowl", [
        (-1.12, fairing_ring(.105, .72, .82, .91)),
        (-.91, fairing_ring(.15, .73, .87, .98)),
        (-.66, fairing_ring(.225, .75, .90, 1.00)),
        (-.48, fairing_ring(.22, .77, .89, .96)),
    ], "PAINT_SIDE")
    for face in tail.data.polygons:
        if face.normal.z > .55:
            material_face(b, tail, face, "PAINT_TOP")
    b.box("Undertray", (-.15, -.99, .69), (.15, -.54, .75), "BLACK")

    # Independent fenders and suspension leave true air around each tire.
    fender(b, "FrontFender", WHEELS["front_z"], .365, .24,
           math.radians(42), math.radians(138), "PAINT_TOP", 10)
    fender(b, "RearHugger", WHEELS["rear_z"], .365, .19,
           math.radians(54), math.radians(125), "BLACK", 8)
    for side in (-1, 1):
        suffix = "L" if side < 0 else "R"
        beam(b, "ForkLeg" + suffix, (side * .118, .43, .96),
             (side * .074, .76, .35), .048, "METAL")
        beam(b, "ForkSlider" + suffix, (side * .074, .63, .58),
             (side * .074, .76, .35), .066, "ENGINE")
        x_cylinder(b, "FrontCaliper" + suffix,
                   (side * .102, .69, .48), .065, .035, 8, "BRAKE")
        x_cylinder(b, "RearCaliper" + suffix,
                   (side * .105, -.66, .45), .050, .030, 8, "BRAKE")

    # Controls, clip-ons, mirrors, pegs, levers and gauge pod.
    beam(b, "TopClamp", (-.18, .39, .985), (.18, .39, .985), .040, "METAL")
    beam(b, "LeftClipOn", (-.08, .39, 1.00), (-.32, .30, 1.045), .030, "BLACK")
    beam(b, "RightClipOn", (.08, .39, 1.00), (.32, .30, 1.045), .030, "BLACK")
    for side in (-1, 1):
        suffix = "L" if side < 0 else "R"
        beam(b, "Grip" + suffix, (side * .28, .315, 1.04),
             (side * .37, .285, 1.055), .045, "RUBBER")
        beam(b, "Lever" + suffix, (side * .28, .31, 1.02),
             (side * .39, .35, .995), .014, "METAL")
        beam(b, "MirrorStem" + suffix, (side * .22, .61, 1.00),
             (side * .38, .70, 1.09), .018, "BLACK")
        panel_prism(b, "Mirror" + suffix, side,
                    [(.66, 1.055), (.75, 1.065), (.77, 1.13), (.68, 1.145)],
                    .355, .41, "BLACK")
        beam(b, "FootPeg" + suffix, (side * .12, -.12, .52),
             (side * .30, -.12, .52), .034, "METAL")
        beam(b, "ControlPedal" + suffix, (side * .20, -.10, .50),
             (side * .29, .01, .48), .016, "METAL")
    b.box("GaugePod", (-.135, .385, 1.03), (.135, .47, 1.10), "BLACK")
    x_cylinder(b, "Tachometer", (-.065, .405, 1.075), .055, .018, 10, "GAUGE")
    x_cylinder(b, "Speedometer", (.065, .405, 1.075), .055, .018, 10, "GAUGE")

    # Four headers flow into paired high silencers.  The chain and guard stay
    # on the body so they do not rotate with the rear wheel.
    header_x = (-.18, -.06, .06, .18)
    for index, x in enumerate(header_x):
        beam(b, "HeaderA" + str(index), (x, .22, .61), (x, .08, .43), .034, "EXHAUST")
        beam(b, "HeaderB" + str(index), (x, .08, .43),
             ((-.16 + index * .105), -.36, .39), .034, "EXHAUST")
    for side in (-1, 1):
        suffix = "L" if side < 0 else "R"
        beam(b, "Collector" + suffix, (side * .16, -.35, .40),
             (side * .255, -.63, .56), .070, "EXHAUST")
        beam(b, "Silencer" + suffix, (side * .255, -.61, .57),
             (side * .255, -1.00, .72), .098, "EXHAUST")
    for rail_z in (.31, .39):
        beam(b, "ChainRun" + str(rail_z), (-.155, -.14, rail_z),
             (-.11, -.72, rail_z), .018, "CHAIN")
    beam(b, "ChainGuard", (-.19, -.29, .46), (-.15, -.72, .43), .035, "BLACK")
    beam(b, "SideStand", (-.17, -.22, .42), (-.31, -.50, .20), .025, "METAL")

    # Exact lamp receivers, indicators, reflector and a small original plate.
    b.panel("HeadlampLeft", [(-.205, 1.121, .825), (-.022, 1.121, .825),
                              (-.028, 1.121, .945), (-.185, 1.121, .955)],
            "HEADLIGHT", (0, 1, 0))
    b.panel("HeadlampRight", [(.022, 1.121, .825), (.205, 1.121, .825),
                               (.185, 1.121, .955), (.028, 1.121, .945)],
            "HEADLIGHT", (0, 1, 0))
    b.box("HeadlampDivider", (-.023, 1.116, .82), (.023, 1.128, .96), "BLACK")
    b.panel("TailLamp", [(.20, -1.121, .805), (-.20, -1.121, .805),
                          (-.17, -1.121, .92), (.17, -1.121, .92)],
            "TAIL_RED", (0, -1, 0))
    for side in (-1, 1):
        suffix = "L" if side < 0 else "R"
        b.box("FrontIndicator" + suffix,
              (side * .35 - .035, .77, .82),
              (side * .35 + .035, .84, .89), "AMBER")
        b.box("RearIndicator" + suffix,
              (side * .255 - .035, -.94, .77),
              (side * .255 + .035, -.87, .84), "AMBER")
    b.panel("Plate", [(-.12, -1.128, .66), (.12, -1.128, .66),
                       (.12, -1.128, .77), (-.12, -1.128, .77)],
            "PLATE", (0, -1, 0))
    return clean_and_join(b, "BODY")


def build_wheel(name, half_width, rear):
    b = VehicleBuilder()
    torus_x(b, "Tire", .292, .053, 0, 18, 6, "TIRE")
    torus_x(b, "Rim", .265, .018, 0, 16, 4, "RIM")
    x_cylinder(b, "Hub", (0, 0, 0), .067, half_width * 1.55, 12, "METAL")
    for side in (-1, 1):
        x = side * half_width * .72
        for spoke in range(6):
            angle = math.tau * (spoke + (0.5 if side > 0 else 0)) / 6
            beam(b, "Spoke", (x, 0, 0),
                 (x, .235 * math.sin(angle), .235 * math.cos(angle)), .025, "RIM")
    if rear:
        x_cylinder(b, "RearDisc", (half_width * .78, 0, 0), .205,
                   .012, 16, "BRAKE")
        x_cylinder(b, "Sprocket", (-half_width * .80, 0, 0), .185,
                   .018, 14, "CHAIN")
        torus_x(b, "SprocketTeeth", .184, .010, -half_width * .83,
                14, 4, "CHAIN")
    else:
        for side in (-1, 1):
            x_cylinder(b, "FrontDisc", (side * half_width * .82, 0, 0),
                       .215, .012, 18, "BRAKE")
    return clean_and_join(b, name)


def build_windscreen():
    b = VehicleBuilder()
    # A thin closed wedge renders from either side with the shared glass pass.
    vertices = [(-.22, .49, 1.015), (.22, .49, 1.015),
                (.145, .66, 1.175), (-.145, .66, 1.175),
                (-.22, .494, 1.015), (.22, .494, 1.015),
                (.145, .664, 1.175), (-.145, .664, 1.175)]
    faces = [(0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1),
             (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)]
    b.add_mesh("Windscreen", vertices, faces, "BLACK")
    return clean_and_join(b, "WINDSHIELD")


def attach_texture(obj, texture, blend_dir):
    image = bpy.data.images.load(str(texture), check_existing=True)
    image.filepath = bpy.path.relpath(str(texture), start=str(blend_dir))
    for slot in obj.material_slots:
        material = slot.material
        material.use_nodes = True
        node = material.node_tree.nodes.new("ShaderNodeTexImage")
        node.image = image
        node.interpolation = "Closest"
        shader = material.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Roughness"].default_value = 1.0
        material.node_tree.links.new(node.outputs["Color"], shader.inputs["Base Color"])


def main():
    source = sys.argv[sys.argv.index("--") + 1:]
    parser = argparse.ArgumentParser()
    parser.add_argument("--model-dir", type=Path, required=True)
    parser.add_argument("--texture", type=Path, required=True)
    args = parser.parse_args(source)
    args.model_dir.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for material in list(bpy.data.materials):
        bpy.data.materials.remove(material)

    body = build_body()
    front = build_wheel("FRONT_WHEEL", WHEELS["front_half_width"], False)
    rear = build_wheel("REAR_WHEEL", WHEELS["rear_half_width"], True)
    windscreen = build_windscreen()
    for obj in (body, front, rear, windscreen):
        attach_texture(obj, args.texture, args.model_dir)

    body_counts = export_emesh(body, args.model_dir / "body.emesh", "fang_venom_v2")
    front_counts = export_emesh(front, args.model_dir / "front_wheel.emesh", "fang_venom_v2")
    rear_counts = export_emesh(rear, args.model_dir / "rear_wheel.emesh", "fang_venom_v2")
    export_emesh(windscreen, args.model_dir / "windshield.emesh", "fang_venom_v2_glass")

    front.location = (0, WHEELS["front_z"], WHEELS["centre_y"])
    rear.location = (0, WHEELS["rear_z"], WHEELS["centre_y"])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.model_dir / "source.blend"))
    print("FANG_VENOM_V2 cooked body=%s front=%s rear=%s" %
          (body_counts, front_counts, rear_counts))


if __name__ == "__main__":
    main()
