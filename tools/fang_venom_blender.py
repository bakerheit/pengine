#!/usr/bin/env python3
"""Build the wheel-less Fang Venom body plus its separate two-wheel mesh."""

import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fang_venom_spec import REGIONS
from vesper_vx91_blender import VehicleBuilder, export_emesh


def beam(builder, name, start, end, width, material):
    direction = (Vector(end) - Vector(start)).normalized()
    reference = Vector((0, 0, 1)) if abs(direction.z) < .92 else Vector((0, 1, 0))
    side = direction.cross(reference).normalized() * width * .5
    up = direction.cross(side).normalized() * width * .5
    points = [tuple(Vector(p) + a * side + b * up)
              for p in (start, end) for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    return builder.add_mesh(name, points,
        [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)], material)


def material_face(builder, obj, face, name):
    material = builder.material(name)
    if material.name not in obj.data.materials:
        obj.data.materials.append(material)
    face.material_index = obj.data.materials.find(material.name)


def map_uvs(obj):
    mesh = obj.data
    uv = mesh.uv_layers.new(name="UVMap")
    for face in mesh.polygons:
        name = mesh.materials[face.material_index].name
        box = REGIONS.get(name, REGIONS["BLACK"])
        x0, y0, x1, y1 = box
        normal = face.normal
        axes = (0, 1) if abs(normal.z) >= max(abs(normal.x), abs(normal.y)) else (
            (0, 2) if abs(normal.y) >= abs(normal.x) else (1, 2))
        points = [mesh.vertices[mesh.loops[i].vertex_index].co for i in face.loop_indices]
        bounds = [(min(p[a] for p in points), max(p[a] for p in points)) for a in axes]
        for loop, point in zip(face.loop_indices, points):
            values = [(point[a] - lo) / (hi - lo) if hi - lo > 1e-6 else .5
                      for a, (lo, hi) in zip(axes, bounds)]
            uv.data[loop].uv = ((x0 + 3 + values[0] * (x1 - x0 - 6)) / 256,
                                1 - (y1 - 3 - values[1] * (y1 - y0 - 6)) / 256)


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
        map_uvs(obj)
    joined = builder.join()
    joined.name = name
    joined.data.name = "FangVenom" + name.title().replace("_", "")
    return joined


def build_body():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for material in list(bpy.data.materials):
        bpy.data.materials.remove(material)
    b = VehicleBuilder()

    # Narrow steel spine and a visible diamond frame.
    b.box("LowerSpine", (-.105, -.73, .36), (.105, .66, .49), "BLACK")
    for side in (-1, 1):
        x = side * .19
        beam(b, "FrameTop", (x, -.52, .62), (x, .38, .91), .045, "FRAME")
        beam(b, "FrameLower", (x, -.52, .62), (x, .27, .48), .045, "FRAME")
        beam(b, "SwingArm", (side*.16, -.13, .52), (side*.11, -.72, .39), .06, "METAL")
        beam(b, "Fork", (side*.115, .43, .96), (side*.075, .73, .39), .052, "METAL")

    # Air-cooled transverse engine with readable block/fins at PSX distance.
    b.box("EngineBlock", (-.255, -.27, .42), (.255, .25, .68), "ENGINE")
    b.box("CylinderHead", (-.28, -.08, .66), (.28, .22, .79), "ENGINE")
    for index, z in enumerate((.47, .52, .57, .62, .69, .735)):
        b.box("EngineFin" + str(index), (-.29, -.285, z), (.29, .255, z+.018), "METAL")
    for side in (-1, 1):
        b.cylinder("EngineCover" + str(side), (side*.275, -.02, .56), .13, .035, 10, "METAL")

    # Faceted lower belly and side fairings; the wheels remain fully separate.
    belly = b.loft("BellyPan", [
        (-.38, [(-.22,.38),(-.29,.47),(.29,.47),(.22,.38)]),
        (.34, [(-.27,.38),(-.32,.54),(.32,.54),(.27,.38)]),
        (.58, [(-.18,.43),(-.24,.65),(.24,.65),(.18,.43)]),
    ], "PAINT_SIDE")
    for face in belly.data.polygons:
        if face.normal.z > .55:
            material_face(b, belly, face, "PAINT_TOP")

    # Tank, pointed cowl and early-90s stepped tail.
    tank = b.loft("FuelTank", [
        (-.34, [(-.18,.69),(-.29,.81),(-.24,1.04),(0,1.10),(.24,1.04),(.29,.81),(.18,.69)]),
        (-.04, [(-.23,.69),(-.31,.84),(-.25,1.10),(0,1.15),(.25,1.10),(.31,.84),(.23,.69)]),
        (.30, [(-.16,.69),(-.25,.82),(-.18,1.03),(0,1.08),(.18,1.03),(.25,.82),(.16,.69)]),
    ], "PAINT_TOP")
    for face in tank.data.polygons:
        if abs(face.normal.x) > .45:
            material_face(b, tank, face, "PAINT_SIDE")
    nose = b.loft("WedgeNose", [
        (.42, [(-.25,.66),(-.31,.80),(-.24,1.05),(0,1.11),(.24,1.05),(.31,.80),(.25,.66)]),
        (.76, [(-.20,.70),(-.27,.82),(-.20,1.04),(0,1.09),(.20,1.04),(.27,.82),(.20,.70)]),
        (1.08, [(-.16,.78),(-.21,.85),(-.17,1.00),(0,1.05),(.17,1.00),(.21,.85),(.16,.78)]),
    ], "NOSE")
    for face in nose.data.polygons:
        if abs(face.normal.x) > .45:
            material_face(b, nose, face, "PAINT_SIDE")
    b.loft("Seat", [(-.58,[(-.24,.80),(-.23,.91),(.23,.91),(.24,.80)]),
                    (-.10,[(-.23,.80),(-.22,.92),(.22,.92),(.23,.80)])], "SEAT")
    tail = b.loft("TailCowl", [(-1.08,[(-.13,.77),(-.16,.92),(.16,.92),(.13,.77)]),
                                (-.58,[(-.24,.78),(-.25,1.00),(.25,1.00),(.24,.78)])], "PURPLE")
    for face in tail.data.polygons:
        if face.normal.z > .4:
            material_face(b, tail, face, "PAINT_TOP")

    # Controls, pegs, exhaust and actual lamp receivers.
    beam(b, "Handlebar", (-.35, .30, 1.05), (.35, .30, 1.05), .035, "BLACK")
    beam(b, "SteeringStem", (0, .45, .92), (0, .30, 1.05), .05, "METAL")
    for side in (-1, 1):
        b.box("Grip"+str(side), (side*.35-.05, .265, 1.025),
              (side*.35+.05, .335, 1.075), "BLACK")
        beam(b, "FootPeg"+str(side), (side*.12, -.10, .55), (side*.31, -.10, .55), .035, "METAL")
        beam(b, "Header"+str(side), (side*.20, .15, .57), (side*.27, -.56, .55), .052, "EXHAUST")
        beam(b, "Muffler"+str(side), (side*.28, -.53, .61), (side*.28, -.98, .72), .09, "EXHAUST")
    b.panel("Headlamp", [(-.20,1.081,.81),(.20,1.081,.81),(.17,1.081,.98),(-.17,1.081,.98)],
            "HEADLIGHT", (0,1,0))
    b.panel("TailLamp", [(.16,-1.081,.82),(-.16,-1.081,.82),(-.14,-1.081,.94),(.14,-1.081,.94)],
            "TAIL_RED", (0,-1,0))
    b.box("Gauge", (-.13,.38,1.075),(.13,.47,1.115),"GAUGE")
    return clean_and_join(b, "BODY")


def build_wheel():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for material in list(bpy.data.materials):
        bpy.data.materials.remove(material)
    b = VehicleBuilder()
    major, minor, around, cross = .305, .060, 14, 5
    vertices = []
    for i in range(around):
        theta = 2*math.pi*i/around
        for j in range(cross):
            phi = 2*math.pi*j/cross
            radius = major + minor*math.cos(phi)
            vertices.append((minor*math.sin(phi), radius*math.sin(theta), radius*math.cos(theta)))
    faces = []
    for i in range(around):
        for j in range(cross):
            a=i*cross+j; c=((i+1)%around)*cross+j
            faces.append((a,c,((i+1)%around)*cross+(j+1)%cross,i*cross+(j+1)%cross))
    b.add_mesh("Tire", vertices, faces, "TIRE")
    for side in (-1, 1):
        x=side*.035
        for spoke in range(7):
            angle=2*math.pi*spoke/7
            beam(b,"Spoke",(x,0,0),(x,.245*math.sin(angle),.245*math.cos(angle)),.026,"RIM")
    # Axle and hubs use the builder's Y-axis cylinder rotated into source X.
    hub=b.cylinder("Hub",(0,0,0),.075,.13,10,"METAL")
    for vertex in hub.data.vertices:
        vertex.co.x,vertex.co.y=vertex.co.y,vertex.co.x
    return clean_and_join(b,"WHEEL")


def attach_texture(obj, texture, blend_dir):
    image=bpy.data.images.load(str(texture),check_existing=True)
    image.filepath=bpy.path.relpath(str(texture),start=str(blend_dir))
    for slot in obj.material_slots:
        material=slot.material; material.use_nodes=True
        node=material.node_tree.nodes.new("ShaderNodeTexImage")
        node.image=image; node.interpolation="Closest"
        shader=material.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Roughness"].default_value=1.0
        material.node_tree.links.new(node.outputs["Color"],shader.inputs["Base Color"])


if __name__ == "__main__":
    args_source=sys.argv[sys.argv.index("--")+1:]
    parser=argparse.ArgumentParser()
    parser.add_argument("--model-dir",type=Path,required=True)
    parser.add_argument("--texture",type=Path,required=True)
    args=parser.parse_args(args_source)
    args.model_dir.mkdir(parents=True,exist_ok=True)
    body=build_body()
    attach_texture(body,args.texture,args.model_dir)
    export_emesh(body,args.model_dir/"body.emesh","fang_venom")
    wheel=build_wheel()
    attach_texture(wheel,args.texture,args.model_dir)
    export_emesh(wheel,args.model_dir/"wheel.emesh","fang_venom_wheel")
    bpy.ops.wm.save_as_mainfile(filepath=str(args.model_dir/"source.blend"))
    print("FANG_VENOM body and separate wheel cooked")
