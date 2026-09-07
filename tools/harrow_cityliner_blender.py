#!/usr/bin/env python3
"""Full-size transit bus shell with four side pockets and direct atlas receivers."""
import argparse
import math
import sys
from pathlib import Path

import bpy
import bmesh

sys.path.insert(0, str(Path(__file__).resolve().parent))
from harrow_cityliner_spec import ATLAS_SIZE, REGIONS, BOUNDS, WHEELS, SHAPE, roof
from vesper_vx91_blender import VehicleBuilder, export_emesh


def stations():
    radius=SHAPE['arch_long_radius']
    height=SHAPE['arch_height_radius']
    samples = [(-5.40, .22), (-4.7, .22), (0., .22), (3.65, .22), (5.02, .22), (5.40, .22)]
    for axle in (WHEELS['rear_z'], WHEELS['front_z']):
        samples += [(axle-radius, .22), (axle-radius, .55)]
        samples += [(axle-radius*math.cos(i*math.pi/10), .55+height*math.sin(i*math.pi/10))
                    for i in range(1,10)]
        samples += [(axle+radius,.55),(axle+radius,.22)]
    # Roof-profile breaks lying above an arch must follow that opening too.
    for i,(z,h) in enumerate(samples):
        if h != .22:
            continue
        for axle in (WHEELS['rear_z'], WHEELS['front_z']):
            if abs(z-axle) < radius-1e-6:
                # Linear interpolation in the same faceted arch curve.
                arc=[(axle-radius*math.cos(j*math.pi/10), .55+height*math.sin(j*math.pi/10))
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
            axes=(1,2) if mat in ('LEFT','RIGHT') else ((0,1) if mat=='TOP' else (0,2))
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
            waist=1.45
            rings.append((z,[(s*x,h) for x,h in (
                (.62,lower),(1.18,lower),(1.25,lower+.035),
                (1.25,waist),(1.23,top-.14),(1.10,top),(.62,top))]))
        obj=b.loft('PocketedBusSide'+str(s),rings,'LEFT' if s<0 else 'RIGHT')
        for name in ('TOP','SHADOW','FRONT','REAR'):
            obj.data.materials.append(b.material(name))
        for face in obj.data.polygons:
            ps=[obj.data.vertices[i].co for i in face.vertices]
            strip=face.index%7
            name=None
            if all(abs(p.y-5.40)<1e-5 for p in ps):name='FRONT'
            elif all(abs(p.y+5.40)<1e-5 for p in ps):name='REAR'
            elif strip in (0,6):name='SHADOW'
            elif strip==5:
                name='TOP'
            if name:face.material_index=obj.data.materials.find(name)
    # A full width crowned roof joins the two sidewalls above every wheel pocket.
    center=b.loft('ContinuousPassengerRoof',[(z,[(-.62,roof(z)-.10),(-.62,roof(z)),
                       (.62,roof(z)),(.62,roof(z)-.10)]) for z in (-5.40,5.02,5.40)],'TOP')
    b.box('NarrowCentralChassis',(-.54,-5.32,.22),(.54,5.32,.40),'SHADOW')
    # Full-height ends provide direct receivers for screen, lamps and vents.
    for name,z in (('REAR',-5.40),('FRONT',5.40)):
        b.panel(name+'Centre',[(-.62,z,.22),(.62,z,.22),(.62,z,roof(z)),(-.62,z,roof(z))],name,(0,1 if z>0 else -1,0))
    for z in (-5.43,5.43):
        ring=[(-1.13,.24),(-1.27,.30),(-1.27,.51),(-1.16,.56),
              (1.16,.56),(1.27,.51),(1.27,.30),(1.13,.24)]
        b.loft('ImpactBumper'+str(z),[(z-.07,ring),(z+.07,ring)],'RUBBER')
    # Modest rooftop ventilation fairing, low enough to preserve the city-bus silhouette.
    b.loft('RoofVent',[(z,[(-.58,3.08),(-.52,3.21),(.52,3.21),(.58,3.08)]) for z in (-3.7,-2.7)],'CREAM')
    for s in (-1,1):
        a,c=sorted((s*1.21,s*1.41))
        b.box('MirrorArm'+str(s),(a,4.94,2.33),(c,5.01,2.40),'RUBBER')
        a,c=sorted((s*1.37,s*1.44))
        mirror=b.box('TallBusMirror'+str(s),(a,4.77,2.10),(c,5.04,2.49),'RUBBER')
        mirror.data.materials.append(b.material('METAL'))
        for f in mirror.data.polygons:
            if all(abs(mirror.data.vertices[i].co.y-4.77)<1e-5 for i in f.vertices):f.material_index=1
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
    body.data.name='HarrowCitylinerBody'
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
    texture=Path(__file__).resolve().parents[1]/'assets/textures/vehicles/harrow_cityliner/body.png'
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
    print('HARROW_CITYLINER',export_emesh(body,args.mesh,'harrow_cityliner'))
