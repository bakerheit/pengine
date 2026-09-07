#!/usr/bin/env python3
"""Author RIDGE with continuous arch walls and single-layer optical surfaces."""
import argparse
import math
import sys
from pathlib import Path
import bmesh
import bpy

sys.path.insert(0,str(Path(__file__).resolve().parent))
from alder_ridge_spec import ATLAS_SIZE, REGIONS, BOUNDS, WHEELS
from vesper_vx91_blender import VehicleBuilder, export_emesh


def height(y):
    return 1.25 if y <= .65 else 1.25-(y-.65)*(.09/1.43)


def material_face(builder,obj,face,name):
    mat=builder.material(name)
    if mat.name not in obj.data.materials: obj.data.materials.append(mat)
    face.material_index=obj.data.materials.find(mat.name)


def body_side(b,s):
    # Four real pockets: only the outboard wall follows this lower contour.
    # The central hood and rear waist remain uninterrupted above them.
    samples=[(-2.08,.32)]
    for axle in (WHEELS['rear_z'],WHEELS['front_z']):
        samples += [(axle-.49,.32),(axle-.49,.39)]
        samples += [(axle-.49*math.cos(i*math.pi/10),.39+.43*math.sin(i*math.pi/10))
                    for i in range(1,10)]
        samples += [(axle+.49,.39),(axle+.49,.32)]
    samples += [(-.65,.32),(.65,.32),(2.08,.32)]
    samples.sort()
    sections=[]
    for y,low in samples:
        flare=.07*max(0,1-min(abs(y-a) for a in (1.24,-1.40))/.65)
        top=height(y)
        ring=[(.56,low),(.97+flare,low),(.99+flare,low+.045),
              (.99+flare,low+.12),(.99,top-.09),(.95,top),(.56,top)]
        sections.append((y,[(s*x,z) for x,z in ring]))
    obj=b.loft('IntegratedArchWall'+str(s),sections,'SIDE')
    for face in obj.data.polygons:
        if face.normal.z < -.5: material_face(b,obj,face,'SHADOW')
        elif face.normal.z > .5 and min(obj.data.vertices[i].co.z for i in face.vertices)>1.:
            material_face(b,obj,face,'TOP')


def uvs(obj):
    mesh=obj.data
    uv=mesh.uv_layers.new(name='UVMap')
    for face in mesh.polygons:
        name=mesh.materials[face.material_index].name
        n=face.normal
        axes=(0,1) if abs(n.z)>=max(abs(n.x),abs(n.y)) else ((0,2) if abs(n.y)>abs(n.x) else (1,2))
        if name in ('SIDE','CABIN'): axes=(1,2)
        if name in ('FRONT','REAR','WINDSHIELD','BACK_GLASS'): axes=(0,2)
        if name=='TOP': axes=(0,1)
        points=[mesh.vertices[mesh.loops[i].vertex_index].co for i in face.loop_indices]
        bounds=BOUNDS.get(name,tuple((min(p[a] for p in points),max(p[a] for p in points)) for a in axes))
        x0,y0,x1,y1=REGIONS[name]
        for i,p in zip(face.loop_indices,points):
            ab=[(p[a]-lo)/(hi-lo) if hi-lo>1e-6 else .5 for a,(lo,hi) in zip(axes,bounds)]
            uv.data[i].uv=((x0+2+ab[0]*(x1-x0-4))/256,1-(y1-2-ab[1]*(y1-y0-4))/256)


def build_vehicle():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    b=VehicleBuilder()
    b.box('NarrowChassis',(-.47,-2.04,.29),(.47,2.04,.43),'SHADOW')
    for s in (-1,1): body_side(b,s)
    b.loft('ContinuousHoodAndWaist',[(y,[(-.56,.98),(-.56,height(y)),(.56,height(y)),(.56,.98)])
                                   for y in (-2.08,.65,1.24,2.08)],'TOP')
    # An upright rear wall, long door and tall quarter glass give this its
    # SUV silhouette; this is an independent station layout, not a wagon edit.
    vertices=[(-.95,-2.08,1.25),(.95,-2.08,1.25),(-.95,.65,1.25),(.95,.65,1.25),
              (-.86,-1.96,1.99),(.86,-1.96,1.99),(-.86,.36,1.99),(.86,.36,1.99)]
    cabin=b.add_mesh('UprightTwoDoorCabin',vertices,
                    [(0,2,3,1),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)],'TOP')
    for face in cabin.data.polygons:
        name={1:'BACK_GLASS',2:'WINDSHIELD',3:'CABIN',4:'CABIN'}.get(face.index)
        if name: material_face(b,cabin,face,name)
    roof=[(-.86,1.99),(-.83,2.025),(-.74,2.04),(.74,2.04),(.83,2.025),(.86,1.99)]
    b.loft('PlainChamferedRoof',[(-1.96,roof),(.36,roof)],'TOP')
    b.box('FrontFascia',(-.56,1.98,.32),(.56,2.08,.98),'SIDE')
    b.box('Tailgate',(-.56,-2.08,.32),(.56,-1.98,.98),'SIDE')
    for end in (-1,1):
        ring=[(-1.0,.32),(-1.045,.36),(-1.045,.52),(-.99,.56),(.99,.56),(1.045,.52),(1.045,.36),(1.,.32)]
        b.loft('IntegratedBumper'+str(end),[(end*2.14-.06,ring),(end*2.14+.06,ring)],'CLADDING')
    for s in (-1,1):
        lo,hi=sorted((s*.93,s*1.055))
        b.box('MirrorStem'+str(s),(lo,.46,1.29),(hi,.53,1.35),'BLACK')
        lo,hi=sorted((s*1.035,s*1.10))
        mirror=b.box('Mirror'+str(s),(lo,.37,1.32),(hi,.61,1.51),'BLACK')
        for face in mirror.data.polygons:
            if face.normal.y < -.9: material_face(b,mirror,face,'METAL')
    for obj in b.objects:
        for face in obj.data.polygons:
            ys=[obj.data.vertices[i].co.y for i in face.vertices]
            if all(abs(y-2.08)<1e-5 for y in ys) and face.normal.y>.9:
                material_face(b,obj,face,'FRONT')
            elif all(abs(y+2.08)<1e-5 for y in ys) and face.normal.y<-.9:
                material_face(b,obj,face,'REAR')
        bm=bmesh.new();bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7)
        bm.to_mesh(obj.data);bm.free();obj.data.update()
        uvs(obj)
    body=b.join();body.data.name='AlderRidgeBody'
    for s in (-1,1):
        for label,z in (('F',1.24),('R',-1.40)):
            empty=bpy.data.objects.new('WHEEL_'+label+('L' if s<0 else 'R'),None)
            empty.location=(s*.98,z,.39)
            bpy.context.collection.objects.link(empty)
    assert len([o for o in bpy.context.scene.objects if o.type=='MESH'])==1
    return body


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--mesh',type=Path,required=True);p.add_argument('--blend',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    body=build_vehicle();args.blend.parent.mkdir(parents=True,exist_ok=True)
    texture=Path(__file__).resolve().parents[1]/'assets/textures/vehicles/alder_ridge/body.png'
    atlas=bpy.data.images.load(str(texture),check_existing=True)
    atlas.filepath=bpy.path.relpath(str(texture),start=str(args.blend.parent))
    for slot in body.material_slots:
        mat=slot.material;mat.use_nodes=True
        node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas;node.interpolation='Closest'
        shader=mat.node_tree.nodes.get('Principled BSDF');shader.inputs['Roughness'].default_value=1.
        mat.node_tree.links.new(node.outputs['Color'],shader.inputs['Base Color'])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print('ALDER_RIDGE',export_emesh(body,args.mesh,'alder_ridge'))
