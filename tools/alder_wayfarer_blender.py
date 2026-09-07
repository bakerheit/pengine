#!/usr/bin/env python3
"""Build the Wayfarer's continuous open-arch sides and long wagon greenhouse."""
import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from alder_wayfarer_spec import ATLAS_SIZE, REGIONS, SURFACE_REGIONS, SURFACE_BOUNDS, WHEELS
from harrow_workman_blender import PickupBuilder as VehicleBuilder
from vesper_vx91_blender import export_emesh


def shoulder(y):
    return 1.13 - max(0.0, y - .80) * (.09 / 1.82)


def map_uvs(obj):
    mesh = obj.data
    uv = mesh.uv_layers.new(name="UVMap")
    for face in mesh.polygons:
        name = mesh.materials[face.material_index].name
        x0, y0, x1, y1 = SURFACE_REGIONS.get(name, REGIONS.get(name))
        n = face.normal
        axes = (0, 1) if abs(n.z) >= max(abs(n.x), abs(n.y)) else (
            (0, 2) if abs(n.y) > abs(n.x) else (1, 2))
        points = [mesh.vertices[mesh.loops[i].vertex_index].co for i in face.loop_indices]
        if name in SURFACE_BOUNDS:
            axes = (1, 2) if name == "GLASS" else (0, 2)
            bounds = SURFACE_BOUNDS[name]
        elif name == "SIDE":
            # Keep trim continuous even on the sloped arch bevels. Per-face
            # projection there would stamp entire door details on each facet.
            axes = (1, 2)
            bounds = ((-2.62, 2.62), (.25, 1.13))
        elif name == "PAINT" and axes == (0, 1):
            bounds = ((-1.04, 1.04), (-2.62, 2.62))
        else:
            bounds = tuple((min(p[a] for p in points), max(p[a] for p in points)) for a in axes)
        for loop, p in zip(face.loop_indices, points):
            ab = [(p[a] - lo) / (hi - lo) if hi - lo > 1e-6 else .5
                  for a, (lo, hi) in zip(axes, bounds)]
            uv.data[loop].uv = ((x0 + 2 + ab[0]*(x1-x0-4))/ATLAS_SIZE,
                               1 - (y1-2-ab[1]*(y1-y0-4))/ATLAS_SIZE)


def build_side(builder, side):
    samples = [(-2.62, .25)]
    for axle in (WHEELS["rear_z"], WHEELS["front_z"]):
        samples.extend([(axle-.59, .25), (axle-.59, .43)])
        samples.extend((axle-.59*math.cos(i*math.pi/8), .43+.51*math.sin(i*math.pi/8))
                       for i in range(1, 8))
        samples.extend([(axle+.59, .43), (axle+.59, .25)])
    samples.extend([(.80, .25), (2.62, .25)])
    samples.sort(key=lambda p: p[0])
    sections = []
    for y, lower in samples:
        top = shoulder(y)
        ring = ((.68, lower), (.98, lower), (1.04, lower+.035),
                (1.04, top-.065), (.90, top), (.68, top))
        sections.append((y, [(side*x, z) for x, z in ring]))
    obj = builder.loft("ContinuousBodySide"+str(side), sections, "SIDE")
    obj.data.materials.append(builder.material("PAINT"))
    obj.data.materials.append(builder.material("SHADOW"))
    for face in obj.data.polygons:
        strip = face.index % 6
        if face.index < (len(sections)-1)*6 and strip in (0, 5):
            face.material_index = 2
        elif face.normal.z > .5 and all(obj.data.vertices[i].co.z > .95 for i in face.vertices):
            face.material_index = 1


def build_vehicle():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    b = VehicleBuilder()
    b.box("NarrowChassis", (-.48, -2.50, .24), (.48, 2.50, .38), "SHADOW")
    for side in (-1, 1):
        build_side(b, side)
    # This upper central slab meets, rather than overlaps, the fender crowns.
    b.loft("HoodAndWaist", [(y, [(-.68,.96),(-.68,shoulder(y)),
                                  (.68,shoulder(y)),(.68,.96)])
                             for y in (-2.62,.80,2.62)], "PAINT")
    # Cabin sides taper in plan and height. The roof is a separate cream cap
    # with a shared boundary, not two coplanar roof skins.
    vertices = [(-.90,-2.46,1.13),(.90,-2.46,1.13),
                (-.90,.80,1.13),(.90,.80,1.13),
                (-.82,-2.08,1.70),(.82,-2.08,1.70),
                (-.82,.26,1.70),(.82,.26,1.70)]
    b.add_mesh("WagonCabin", vertices,
               [(0,2,3,1),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)], "PAINT")
    roof_ring = [(-.82,1.70),(-.81,1.745),(-.70,1.77),
                 (.70,1.77),(.81,1.745),(.82,1.70)]
    b.loft("CreamRoof", [(-2.08,roof_ring),(.26,roof_ring)], "CREAM")
    for s in (-1,1):
        a,c = sorted((s*.96,s*1.09))
        b.box("MirrorStem"+str(s),(a,.54,1.14),(c,.59,1.19),"RUBBER")
        a,c = sorted((s*1.07,s*1.15))
        b.box("Mirror"+str(s),(a,.47,1.18),(c,.65,1.33),"RUBBER")
        # Rack feet land on the roof; rails and crossbars meet without skins
        # stacked on the same plane. No luggage or other extra cargo.
        for y in (-1.72,-.18):
            a,c=sorted((s*.64,s*.70))
            b.box("RackFoot"+str((s,y)),(a,y,1.765),(c,y+.09,1.825),"RUBBER")
        a,c=sorted((s*.65,s*.69))
        b.box("RackRail"+str(s),(a,-1.80,1.825),(c,-.06,1.88),"CHROME")
    for y in (-1.67,-.18):
        b.box("RackCrossbar"+str(y),(-.65,y,1.825),(.65,y+.035,1.865),"CHROME")
    # End panels stop short of the side skin and do not cross the wheel holes.
    b.box("FrontFascia",(-.68,2.52,.35),(.68,2.62,.96),"PAINT")
    b.box("RearFascia",(-.68,-2.62,.35),(.68,-2.52,.96),"PAINT")
    for y in (-2.685,2.685):
        ring=[(-1.02,.23),(-1.07,.28),(-1.07,.42),(1.07,.42),(1.07,.28),(1.02,.23)]
        b.loft("RubberBumper"+str(y),[(y-.065,ring),(y+.065,ring)],"RUBBER")
    # Optical details are baked into these receiver faces. Never add a second
    # glass/lens skin: its separate vertices would bend through the receiver.
    for obj in b.objects:
        for face in obj.data.polygons:
            material = None
            if obj.name == "WagonCabin":
                material = {1:"REAR_GLASS", 2:"WINDSHIELD", 3:"GLASS", 4:"GLASS"}.get(face.index)
            elif obj.name.startswith("Mirror") and not obj.name.startswith("MirrorStem"):
                xs=[obj.data.vertices[i].co.x for i in face.vertices]
                if all(abs(abs(x)-1.15)<1e-5 for x in xs):material="CHROME"
            elif obj.name.startswith("RubberBumper"):
                ys=[obj.data.vertices[i].co.y for i in face.vertices]
                if all(abs(abs(y)-2.75)<1e-5 for y in ys):material="BUMPER_FACE"
            else:
                ys = [obj.data.vertices[i].co.y for i in face.vertices]
                if all(abs(y-2.62)<1e-5 for y in ys) and face.normal.y>.9:
                    material = "FRONT_FACE"
                elif all(abs(y+2.62)<1e-5 for y in ys) and face.normal.y<-.9:
                    material = "TAILGATE"
            if material:
                mat = b.material(material)
                if mat.name not in obj.data.materials:
                    obj.data.materials.append(mat)
                face.material_index = obj.data.materials.find(mat.name)
        bm=bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7)
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()
        map_uvs(obj)
    body=b.join()
    body.data.name="AlderWayfarerBody"
    for side in (-1,1):
        for axle in (WHEELS["front_z"],WHEELS["rear_z"]):
            empty=bpy.data.objects.new(f"WHEEL_{side}_{axle}",None)
            empty.location=(side*WHEELS["x"],axle,WHEELS["arch_y"])
            bpy.context.collection.objects.link(empty)
    assert len([o for o in bpy.context.scene.objects if o.type=="MESH"])==1
    return body


if __name__ == "__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--mesh",type=Path,required=True)
    parser.add_argument("--blend",type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index("--")+1:])
    body=build_vehicle()
    args.blend.parent.mkdir(parents=True,exist_ok=True)
    texture_path=Path(__file__).resolve().parents[1]/"assets/textures/vehicles/alder_wayfarer/body.png"
    atlas=bpy.data.images.load(str(texture_path),check_existing=True)
    atlas.filepath=bpy.path.relpath(str(texture_path),start=str(args.blend.parent))
    for slot in body.material_slots:
        material=slot.material
        material.use_nodes=True
        shader=material.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Roughness"].default_value=1.0
        texture=material.node_tree.nodes.new("ShaderNodeTexImage")
        texture.image=atlas
        texture.interpolation="Closest"
        material.node_tree.links.new(texture.outputs["Color"],shader.inputs["Base Color"])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print("ALDER_WAYFARER",export_emesh(body,args.mesh,"alder_wayfarer"))
