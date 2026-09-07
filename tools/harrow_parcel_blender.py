#!/usr/bin/env python3
"""Continuous van shell with four side pockets and directly textured receivers."""
import argparse
import math
import sys
from pathlib import Path

import bpy
import bmesh

sys.path.insert(0, str(Path(__file__).resolve().parent))
from harrow_parcel_spec import ATLAS_SIZE, REGIONS, BOUNDS, WHEELS, SHAPE, roof
from vesper_vx91_blender import VehicleBuilder, export_emesh


def stations():
    radius=SHAPE['arch_long_radius']
    height=SHAPE['arch_height_radius']
    samples = [(-2.54, .26), (-.12, .26), (.72, .26), (1.46, .26), (2.54, .26)]
    for axle in (WHEELS['rear_z'], WHEELS['front_z']):
        samples += [(axle-radius, .26), (axle-radius, .43)]
        samples += [(axle-radius*math.cos(i*math.pi/10), .43+height*math.sin(i*math.pi/10))
                    for i in range(1,10)]
        samples += [(axle+radius,.43),(axle+radius,.26)]
    # Roof-profile breaks lying above an arch must follow that opening too.
    for i,(z,h) in enumerate(samples):
        if h != .26:
            continue
        for axle in (WHEELS['rear_z'], WHEELS['front_z']):
            if abs(z-axle) < radius-1e-6:
                # Linear interpolation in the same faceted arch curve.
                arc=[(axle-radius*math.cos(j*math.pi/10), .43+height*math.sin(j*math.pi/10))
                     for j in range(11)]
                for (a,ha),(b,hb) in zip(arc,arc[1:]):
                    if a<=z<=b:
                        samples[i]=(z,ha+(hb-ha)*(z-a)/(b-a))
                        break
    # Preserve the authored vertical edge order at the end of each arch.
    # Sorting by height too would ramp the sill up toward the next station.
    return sorted(samples,key=lambda p:p[0])


def map_uvs(obj):
    mesh=obj.data
    uv=mesh.uv_layers.new(name='UVMap')
    for face in mesh.polygons:
        mat=mesh.materials[face.material_index].name
        x0,y0,x1,y1=REGIONS[mat]
        pts=[mesh.vertices[mesh.loops[i].vertex_index].co for i in face.loop_indices]
        if mat in BOUNDS:
            axes=(1,2) if mat=='SIDE' else ((0,1) if mat=='TOP' else (0,2))
            bounds=BOUNDS[mat]
        else:
            n=face.normal
            axes=(0,1) if abs(n.z)>=max(abs(n.x),abs(n.y)) else ((0,2) if abs(n.y)>abs(n.x) else (1,2))
            bounds=tuple((min(p[a] for p in pts),max(p[a] for p in pts)) for a in axes)
        for loop,p in zip(face.loop_indices,pts):
            ab=[(p[a]-lo)/(hi-lo) if hi-lo>1e-7 else .5 for a,(lo,hi) in zip(axes,bounds)]
            uv.data[loop].uv=((x0+2+ab[0]*(x1-x0-4))/ATLAS_SIZE,
                              1-(y1-2-ab[1]*(y1-y0-4))/ATLAS_SIZE)


def build_vehicle():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    b=VehicleBuilder()
    sections=stations()
    for s in (-1,1):
        rings=[]
        for z,lower in sections:
            top=roof(z)
            waist=min(1.25,top-.15)
            rings.append((z,[(s*x,h) for x,h in (
                (.54,lower),(.99,lower),(1.04,lower+.035),
                (1.04,waist),(1.01,top-.10),(.92,top),(.54,top))]))
        obj=b.loft('PocketedVanSide'+str(s),rings,'SIDE')
        for name in ('TOP','SHADOW','FRONT','REAR','SCREEN'):
            obj.data.materials.append(b.material(name))
        for face in obj.data.polygons:
            ps=[obj.data.vertices[i].co for i in face.vertices]
            strip=face.index%7
            name=None
            if all(abs(p.y-2.54)<1e-5 for p in ps):name='FRONT'
            elif all(abs(p.y+2.54)<1e-5 for p in ps):name='REAR'
            elif strip in (0,6):name='SHADOW'
            elif strip==5:
                name='SCREEN' if min(p.y for p in ps)>=.71999 and max(p.y for p in ps)<=1.46001 else 'TOP'
            if name:face.material_index=obj.data.materials.find(name)
    # Full-length central upper deck meets the side crowns at x=+/-.54.
    # It never drops across a wheel pocket, so the hood remains broad and solid.
    center=b.loft('ContinuousRoofWindshieldHood',[(z,[(-.54,roof(z)-.08),(-.54,roof(z)),
                       (.54,roof(z)),(.54,roof(z)-.08)]) for z in (-2.54,.72,1.46,2.54)],'TOP')
    for name in ('SCREEN','FRONT','REAR'):
        center.data.materials.append(b.material(name))
    for f in center.data.polygons:
        ps=[center.data.vertices[i].co for i in f.vertices]
        name=None
        if all(abs(p.y+2.54)<1e-5 for p in ps):name='REAR'
        elif all(abs(p.y-2.54)<1e-5 for p in ps):name='FRONT'
        elif f.index==5:name='SCREEN'
        if name:f.material_index=center.data.materials.find(name)
    b.box('NarrowCentralChassis',(-.47,-2.48,.25),(.47,2.48,.38),'SHADOW')
    # End closures land between the side walls and below the continuous roof.
    rear=b.box('TwinCargoDoorReceiver',(-.54,-2.54,.26),(.54,-2.46,2.30),'REAR')
    front=b.box('FrontFasciaReceiver',(-.54,2.46,.26),(.54,2.54,1.14),'FRONT')
    for obj,name,z in ((rear,'REAR',-2.54),(front,'FRONT',2.54)):
        obj.data.materials.append(b.material('SHADOW'))
        for f in obj.data.polygons:
            if not all(abs(obj.data.vertices[i].co.y-z)<1e-5 for i in f.vertices):f.material_index=1
    for z in (-2.62,2.62):
        ring=[(-1.00,.26),(-1.07,.31),(-1.07,.48),(-1.00,.52),
              (1.00,.52),(1.07,.48),(1.07,.31),(1.00,.26)]
        b.loft('RubberBumper'+str(z),[(z-.08,ring),(z+.08,ring)],'RUBBER')
    for s in (-1,1):
        a,c=sorted((s*1.03,s*1.10))
        b.box('MirrorStem'+str(s),(a,1.06,1.42),(c,1.12,1.49),'RUBBER')
        a,c=sorted((s*1.085,s*1.13))
        mirror=b.box('Mirror'+str(s),(a,.99,1.45),(c,1.19,1.69),'RUBBER')
        mirror.data.materials.append(b.material('METAL'))
        for f in mirror.data.polygons:
            if all(abs(mirror.data.vertices[i].co.y-.99)<1e-5 for i in f.vertices):f.material_index=1
    for obj in b.objects:
        bm=bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7)
        bm.to_mesh(obj.data)
        bm.free()
        obj.data.update()
        map_uvs(obj)
    body=b.join()
    body.data.name='HarrowParcelBody'
    for side in (-1,1):
        for axle in (WHEELS['front_z'],WHEELS['rear_z']):
            empty=bpy.data.objects.new(f'WHEEL_{side}_{axle}',None)
            empty.location=(side*WHEELS['x'],axle,WHEELS['arch_y'])
            bpy.context.collection.objects.link(empty)
    assert len([o for o in bpy.context.scene.objects if o.type=='MESH'])==1
    assert len([o for o in bpy.context.scene.objects if o.type=='EMPTY'])==4
    return body


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--mesh',type=Path,required=True)
    p.add_argument('--blend',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    body=build_vehicle()
    args.blend.parent.mkdir(parents=True,exist_ok=True)
    texture=Path(__file__).resolve().parents[1]/'assets/textures/vehicles/harrow_parcel/body.png'
    atlas=bpy.data.images.load(str(texture),check_existing=True)
    atlas.filepath=bpy.path.relpath(str(texture),start=str(args.blend.parent))
    for slot in body.material_slots:
        mat=slot.material
        mat.use_nodes=True
        shader=mat.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Roughness'].default_value=1
        node=mat.node_tree.nodes.new('ShaderNodeTexImage')
        node.image=atlas
        node.interpolation='Closest'
        mat.node_tree.links.new(node.outputs['Color'],shader.inputs['Base Color'])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print('HARROW_PARCEL',export_emesh(body,args.mesh,'harrow_parcel'))
