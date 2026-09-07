#!/usr/bin/env python3
"""Explicit open-arch topology; no full-width wheel Boolean or inherited car body."""
import argparse
import math
import sys
from pathlib import Path

import bpy
import bmesh
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from harrow_workman_spec import ATLAS_SIZE, REGIONS, WHEELS, DOOR, DRIVER
from vesper_vx91_blender import VehicleBuilder, export_emesh


class PickupBuilder(VehicleBuilder):
    def panel(self, name, vertices, material, desired):
        obj = super().panel(name, vertices, material, desired)
        # Recalculating an isolated open face has no reliable "outside".
        # Preserve the authored direction instead of accepting Blender's guess.
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        for face in bm.faces:
            if face.normal.dot(Vector(desired)) < 0:
                face.normal_flip()
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()
        return obj


def map_object(obj):
    mesh = obj.data
    uv = mesh.uv_layers.new(name="UVMap")
    for poly in mesh.polygons:
        name = obj.data.materials[poly.material_index].name
        x0, y0, x1, y1 = REGIONS[name]
        n = poly.normal
        axes = (0, 1) if abs(n.z) >= max(abs(n.x), abs(n.y)) else (
            (0, 2) if abs(n.y) > abs(n.x) else (1, 2))
        points = [mesh.vertices[mesh.loops[i].vertex_index].co for i in poly.loop_indices]
        if obj.name in ("DriverDoorPaint", "DriverSillPaint"):
            axes=(1,2); bounds=((- .43,.92),(.34,1.21))
        elif name == "BODY_SIDE":
            bounds = ((-2.54, 2.54), (0.28, 1.99)) if axes == (1, 2) else (
                ((-1.02, 1.02), (0.28, 1.99)) if axes == (0, 2) else
                ((-1.02, 1.02), (-2.54, 2.54)))
        elif name == "BODY_TOP" and axes == (0, 1):
            bounds = ((-1.02, 1.02), (-2.54, 2.54))
        elif name == "BED" and axes == (0, 1):
            bounds = ((-0.9, 0.9), (-2.54, -0.5))
        else:
            bounds = tuple((min(p[a] for p in points), max(p[a] for p in points)) for a in axes)
        for i, p in zip(poly.loop_indices, points):
            ab = [(p[a] - lo) / (hi - lo) if hi-lo > 1e-6 else 0.5
                  for a, (lo, hi) in zip(axes, bounds)]
            uv.data[i].uv = ((x0+2+ab[0]*(x1-x0-4))/ATLAS_SIZE,
                            1-(y1-2-ab[1]*(y1-y0-4))/ATLAS_SIZE)


def fender(b, side, axle, front):
    # A solid upper wall follows the opening. Its lower boundary is never
    # capped across the wheel, while the broad hood remains above the tire.
    samples = [(-0.60, 0.28), (-0.60, 0.45)]
    samples += [(-0.60*math.cos(i*math.pi/8), 0.45+0.55*math.sin(i*math.pi/8))
                for i in range(1, 8)]
    samples += [(0.60, 0.45), (0.60, 0.28)]
    ends = (.99, 2.54) if front else (-2.54, -.54)
    samples = [(ends[0]-axle,.28)] + samples + [(ends[1]-axle,.28)]
    sections = []
    for dy, bottom in samples:
        top = (1.25 - (axle+dy-0.99)*0.07) if front else 1.22
        ring = (
            (0.87, bottom), (1.00, bottom), (1.02, bottom+0.045),
            (1.02, top-0.07), (0.96, top), (0.87, top)) if front else (
            (0.87, bottom), (1.00, bottom), (1.02, bottom+0.045),
            (1.02, 1.22), (0.86, 1.22), (0.86, 1.17), (0.87, 1.17))
        sections.append((axle+dy, [(side*x, h) for x, h in ring]))
    obj=b.loft(f"{'Front' if front else 'Rear'}Fender{side}", sections, "BODY_SIDE")
    if not front:
        # The rail is part of the wall section, not a box sharing its top skin.
        obj.data.materials.append(b.material("BODY_TOP"))
        for face in obj.data.polygons:
            if all(obj.data.vertices[i].co.z >= 1.16999 for i in face.vertices):
                face.material_index=1


def side_panel(b, name, side, yz, material, x=0.964):
    b.panel(name, [(side*x, y, z) for y,z in yz], material, (side, 0, 0))


def shell_panel(b, name, points, thickness, material="BODY_SIDE"):
    """Thin capped wall, never a solid box filling the passenger compartment."""
    n=len(points)
    vertices=points+[tuple(Vector(p)+Vector(thickness)) for p in points]
    faces=[tuple(range(n)),tuple(range(2*n-1,n-1,-1))]
    faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return b.add_mesh(name,vertices,faces,material)


def cab_side_x(height):
    return .96-(height-1.24)*(.1/.69)


def window_frame(b, name, outer, inner, thickness):
    """Solid rim around a real opening, with no opaque face behind the pane."""
    points = outer + inner
    points += [tuple(p[i] + thickness[i] for i in range(3)) for p in points]
    faces = []
    for i in range(4):
        j = (i+1) % 4
        faces += [(i,j,j+4,i+4),(i+8,i+12,j+12,j+8),
                  (i,i+8,j+8,j),(i+4,j+4,j+12,i+12)]
    return b.add_mesh(name, points, faces, "BODY_SIDE")


def hollow_cab(b):
    # Preserve the lower exterior and the chamfered 1.99 m roof. Only the
    # source solid cab volumes are replaced; hood, bed and wheels stay intact.
    b.box("CabFloor",(-.96,-.50,.28),(.96,.99,DRIVER['floor_y']),"BODY_SIDE")
    b.box("CabRearLower",(-.96,-.50,.52),(.96,-.43,1.24),"BODY_SIDE")
    b.box("CabFirewall",(-.96,.92,.52),(.96,.99,1.24),"BODY_SIDE")
    b.box("PassengerLower",(-.96,-.43,.52),(-.88,.92,1.24),"BODY_SIDE")
    roof=[(-.86,1.93),(-.78,1.99),(.78,1.99),(.86,1.93)]
    b.loft("CabRoof",[(-.50,roof),(.53,roof)],"BODY_SIDE")
    rear=[(-.96,1.24),(-.86,1.93),(.86,1.93),(.96,1.24)]
    window_frame(b,"RearWindowFrame",[(x,-.50,h) for x,h in rear],
        [(-.67,-.50,1.40),(-.62,-.50,1.86),(.62,-.50,1.86),(.67,-.50,1.40)],(0,.04,0))
    window_frame(b,"PassengerWindowFrame",[(-cab_side_x(h),z,h) for z,h in
        ((-.50,1.24),(.99,1.24),(.53,1.93),(-.50,1.93))],
        [(-cab_side_x(h),z,h) for z,h in ((-.40,1.31),(.83,1.31),(.485,1.85),(-.40,1.85))],(.045,0,0))
    shell_panel(b,"DriverRearPillar",[(cab_side_x(h),z,h) for z,h in
        ((-.50,1.24),(-.43,1.24),(-.43,1.93),(-.50,1.93))],(-.045,0,0))
    shell_panel(b,"DriverFrontPillar",[(cab_side_x(h),z,h) for z,h in
        ((.92,1.24),(.99,1.24),(.53,1.93),(.49,1.93))],(-.045,0,0))
    window_frame(b,"WindshieldFrame",[(-.96,.99,1.24),(.96,.99,1.24),
        (.86,.53,1.93),(-.86,.53,1.93)],
        [(-.855,.95,1.30),(.855,.95,1.30),(.765,.56,1.885),(-.765,.56,1.885)],(0,-.025,0))
    b.box("Dashboard",(-.85,.70,1.12),(.85,.925,1.26),"DASH")
    # Two seats leave legroom and a clear entry path beside the driver cushion.
    for x in (-.43,.43):
        b.box("SeatBase"+str(x),(x-.24,-.35,.52),(x+.24,.12,.66),"BLACK")
        b.box("SeatCushion"+str(x),(x-.24,-.35,.66),(x+.24,.18,.80),"DASH")
        b.box("SeatBack"+str(x),(x-.24,-.43,.73),(x+.24,-.37,1.40),"DASH")
    verts=[]
    for z in (.445,.475):
        for radius in (.125,.095):
            verts += [(.43+radius*math.cos(i*math.tau/8),z,
                       1.20+radius*math.sin(i*math.tau/8)) for i in range(8)]
    faces=[]
    for i in range(8):
        j=(i+1)%8
        faces += [(i,j,j+8,i+8),(i+16,i+24,j+24,j+16),
                  (i,j,j+16,i+16),(i+8,i+24,j+24,j+8)]
    b.add_mesh("DriverSteeringRim",verts,faces,"BLACK")
    b.box("SteeringSpoke",(.32,.452,1.19),(.54,.468,1.21),"METAL")
    b.box("SteeringColumn",(.41,.46,1.18),(.45,.73,1.22),"BLACK")
    for x in (.33,.53):
        b.box("Pedal"+str(x),(x-.04,.65,.535),(x+.04,.75,.565),"METAL")


def driver_door(b):
    panel=b.box("DriverDoorPanel",(.88,-.43,.52),(.96,.92,1.24),"BODY_SIDE")
    panel.data.materials.append(b.material("DASH"))
    for face in panel.data.polygons:
        if face.normal.x<-.5: face.material_index=1
    # A capped ring forms the rolled-down window aperture. Its front edge
    # follows the existing windshield slope; mirror anchors move with it.
    outer=[(-.43,1.24),(.92,1.24),(.49,1.93),(-.43,1.93)]
    inner=[(-.39,1.31),(.82,1.31),(.455,1.85),(-.39,1.85)]
    points=[(cab_side_x(h),z,h) for z,h in outer+inner]
    points += [(x-.045,z,h) for x,z,h in points]
    faces=[]
    for i in range(4):
        j=(i+1)%4
        faces += [(i,j,j+4,i+4),(i+8,i+12,j+12,j+8),
                  (i,i+8,j+8,j),(i+4,j+4,j+12,i+12)]
    frame=b.add_mesh("DriverWindowFrame",points,faces,"BODY_SIDE")
    b.panel("DriverGlass",[(cab_side_x(h)-.018,z,h) for z,h in inner],"GLASS",(1,0,0))
    return [panel,frame]


def build_vehicle(articulated=False):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    if articulated:
        for material in list(bpy.data.materials): bpy.data.materials.remove(material)
    b = PickupBuilder()
    door_parts=[]
    # Narrow spine deliberately stays clear of the steered front tire volume.
    b.box("Chassis", (-0.49,-2.5,0.25), (0.49,2.5,0.40), "SHADOW")
    # Shared boundary with the fender crown: no overlapping sloped faces.
    b.loft("Hood", [(y, [(-.87,.99),(-.87,h),(.87,h),(.87,.99)])
                    for y,h in ((.99,1.25),(1.85,1.1898),(2.54,1.1415))], "BODY_TOP")
    if articulated:
        hollow_cab(b)
        door_parts=driver_door(b)
    else:
        b.box("CabLower", (-.96,-.50,.28), (.96,.99,1.24), "BODY_SIDE")
        # Preserve the legacy solid cab export exactly.
        b.loft("CabUpper", [
            (-.50,[(-.96,1.24),(-.86,1.93),(-.78,1.99),(.78,1.99),(.86,1.93),(.96,1.24)]),
            (.53,[(-.96,1.24),(-.86,1.93),(-.78,1.99),(.78,1.99),(.86,1.93),(.96,1.24)]),
        ], "BODY_SIDE")
        b.add_mesh("WindshieldPillars", [(-.96,.53,1.24),(.96,.53,1.24),
            (-.96,.99,1.24),(.96,.99,1.24),(-.86,.53,1.93),(.86,.53,1.93)],
            [(0,2,3,1),(2,4,5,3),(0,4,2),(1,3,5),(0,1,5,4)], "BODY_SIDE")
    b.panel("Windshield", [(-.855,.955,1.30),(.855,.955,1.30),
        (.765,.566,1.885),(-.765,.566,1.885)], "GLASS", (0,1,1))
    b.panel("RearWindow", [(-.67,-.504,1.40),(.67,-.504,1.40),
        (.62,-.504,1.86),(-.62,-.504,1.86)], "GLASS", (0,-1,0))
    for s in (-1,1):
        fender(b,s,1.65,True)
        fender(b,s,-1.55,False)
        # Rails are integrated into the rear wall; the cargo bed stays open.
        a,c = sorted((s*.60,s*.89))
        for ya,yb in ((-2.45,-2.16),(-.94,-.61)):
            b.box("BedFloorWing"+str((s,ya)),(a,ya,.60),(c,yb,.65),"BED")
        # Wheel tub: transverse extrusion of an open-bottom upper half shell.
        verts = [(s*x, -1.55+dy, h) for x in (.59,.88)
                 for dy,h in ((-.61,.65),(-.48,.87),(-.25,1.04),
                              (.25,1.04),(.48,.87),(.61,.65))]
        # Cap the inboard wall only. The outboard end opens into the fender;
        # closing that end would put a vertical plate straight through the tire.
        faces=[tuple(range(5,-1,-1))]
        faces += [(i,i+1,i+7,i+6) for i in range(5)]
        b.add_mesh("BedWheelTub"+str(s),verts,faces,"BED")
        if articulated and s==1:
            side_panel(b,"DriverSillPaint",s,[(-.43,.34),(.92,.34),(.92,.52),(-.43,.52)],"DOOR")
            before=len(b.objects)
            side_panel(b,"DriverDoorPaint",s,[(-.43,.52),(.92,.52),(.92,1.21),(-.43,1.21)],"DOOR")
            door_parts.extend(b.objects[before:])
        else:
            side_panel(b,"Door"+str(s),s,[(-.43,.34),(.92,.34),(.92,1.21),(-.43,1.21)],"DOOR")
        # Window vertices lie just outside the tapered cab side.
        yz=[(-.40,1.31),(.83,1.31),(.485,1.85),(-.40,1.85)]
        if not (articulated and s==1):
            b.panel("SideWindow"+str(s),[(s*(.96-(z-1.24)*(.1/.69)+.005),y,z)
                for y,z in yz],"GLASS",(s,0,0))
            b.panel("VentDivider"+str(s),[(s*(.96-(z-1.24)*(.1/.69)+.009),y,z)
                for y,z in ((.52,1.31),(.54,1.31),(.38,1.85),(.36,1.85))],"BLACK",(s,0,0))
        before=len(b.objects)
        a,c=sorted((s*.94,s*1.05))
        b.box("MirrorArm"+str(s),(a,.73,1.30),(c,.78,1.35),"METAL")
        a,c=sorted((s*1.03,s*1.11))
        b.box("Mirror"+str(s),(a,.64,1.32),(c,.84,1.51),"CLADDING")
        side_panel(b,"MirrorGlass"+str(s),s,[(.66,1.35),(.82,1.35),(.82,1.48),(.66,1.48)],"METAL",1.112)
        if articulated and s==1: door_parts.extend(b.objects[before:])
        side_panel(b,"Marker"+str(s),s,[(2.31,.90),(2.49,.90),(2.49,.99),(2.31,.99)],"AMBER",1.023)
    b.box("BedFloor",(-.60,-2.47,.60),(.60,-.58,.65),"BED")
    b.box("BedBulkhead",(-.87,-.62,.60),(.87,-.55,1.18),"BED")
    b.box("Tailgate",(-.87,-2.54,.30),(.87,-2.46,1.17),"BODY_SIDE")
    b.panel("TailgateFace",[(-.85,-2.544,.44),(.85,-2.544,.44),
        (.85,-2.544,1.16),(-.85,-2.544,1.16)],"TAILGATE",(0,-1,0))
    b.box("TailgateTop",(-.86,-2.54,1.17),(.86,-2.46,1.22),"BODY_TOP")
    b.box("FrontFace",(-.87,2.47,.43),(.87,2.54,1.10),"BODY_SIDE")
    for y in (-2.62,2.62):
        b.loft("Bumper"+str(y),[(y-.08,[(-1.03,.30),(-1.06,.35),(-1.06,.47),
            (1.06,.47),(1.06,.35),(1.03,.30)]),
            (y+.08,[(-1.03,.30),(-1.06,.35),(-1.06,.47),
            (1.06,.47),(1.06,.35),(1.03,.30)])],"METAL")
    b.panel("Grille",[(-.55,2.546,.69),(.55,2.546,.69),(.55,2.546,1.025),(-.55,2.546,1.025)],"GRILLE",(0,1,0))
    for s in (-1,1):
        a,c=sorted((s*.60,s*.96))
        for mat,za,zb in (("HEADLIGHT",.78,1.025),("AMBER",.63,.73)):
            b.panel(mat+str(s),[(a,2.546,za),(c,2.546,za),(c,2.546,zb),(a,2.546,zb)],mat,(0,1,0))
        a,c=sorted((s*.895,s*1.015))
        # Adjacent lens cells, not coplanar white-over-red decals.
        for mat,za,zb in (("TAIL",.65,.70),("REVERSE",.70,.79),("TAIL",.79,1.11)):
            b.panel(mat+str(s),[(a,-2.547,za),(c,-2.547,za),(c,-2.547,zb),(a,-2.547,zb)],mat,(0,-1,0))
    for obj in b.objects:
        bm=bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7)
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()
        map_object(obj)
    if articulated:
        glass_names=["Windshield","RearWindow","SideWindow-1","DriverGlass"]
        glass=[next(obj for obj in b.objects if obj.name==name) for name in glass_names]
        b.objects=[obj for obj in b.objects if obj not in door_parts and obj not in glass]
        body=b.join(); body.name="BODY_OPEN"; body.data.name="HarrowWorkmanOpenBody"
        b.objects=door_parts
        door=b.join(); door.name="DRIVER_DOOR"; door.data.name="HarrowWorkmanDriverDoor"
    else:
        body=b.join()
        body.data.name="HarrowWorkmanBody"
    for side in (-1,1):
        for axle in (WHEELS["front_z"],WHEELS["rear_z"]):
            empty=bpy.data.objects.new(f"WHEEL_{side}_{axle}",None)
            empty.location=(side*WHEELS["x"],axle,WHEELS["arch_y"])
            bpy.context.collection.objects.link(empty)
    assert len([o for o in bpy.context.scene.objects if o.type=="MESH"])==(6 if articulated else 1)
    return (body,door,glass) if articulated else body


if __name__ == "__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--mesh",type=Path,required=True)
    parser.add_argument("--blend",type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index("--")+1:])
    body=build_vehicle()
    args.blend.parent.mkdir(parents=True,exist_ok=True)
    texture_path=Path(__file__).resolve().parents[1]/"assets/textures/vehicles/harrow_workman/body.png"
    atlas=bpy.data.images.load(str(texture_path),check_existing=True)
    atlas.filepath=bpy.path.relpath(str(texture_path),start=str(args.blend.parent))
    for slot in body.material_slots:
        material=slot.material
        material.use_nodes=True
        shader=material.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Roughness"].default_value=1.0
        tex=material.node_tree.nodes.new("ShaderNodeTexImage")
        tex.image=atlas
        tex.interpolation="Closest"
        material.node_tree.links.new(tex.outputs["Color"],shader.inputs["Base Color"])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print("HARROW_WORKMAN",export_emesh(body,args.mesh,"harrow_workman"))
    body_open,door,glass=build_vehicle(articulated=True)
    print("HARROW_WORKMAN_OPEN",export_emesh(body_open,args.mesh.with_name("body_open.emesh"),"harrow_workman"))
    print("HARROW_WORKMAN_DOOR",export_emesh(door,args.mesh.with_name("driver_door.emesh"),"harrow_workman"))

    for name,pane in zip(("windshield","rear_glass","passenger_glass","driver_glass"),glass):
        print("HARROW_WORKMAN_GLASS",name,export_emesh(pane,args.mesh.with_name(name+".emesh"),"harrow_workman"))
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend.with_name("articulated.blend")))
