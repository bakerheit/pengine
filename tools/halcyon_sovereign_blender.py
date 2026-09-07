#!/usr/bin/env python3
"""Author SOVEREIGN with continuous arch walls and single-layer optical surfaces."""
import argparse
import math
import sys
from pathlib import Path
import bmesh
import bpy

sys.path.insert(0,str(Path(__file__).resolve().parent))
from halcyon_sovereign_spec import ATLAS_SIZE, REGIONS, BOUNDS, WHEELS, DOOR, DRIVER
from vesper_vx91_blender import VehicleBuilder, export_emesh
from vehicle_door_builder import split_cabin_side
from vesper_mistral_blender import beam


def height(y):
    return 1.07 if y<=1.70 else 1.07-(y-1.70)*(.13/2.30)

def material_face(builder,obj,face,name):
    mat=builder.material(name)
    if mat.name not in obj.data.materials: obj.data.materials.append(mat)
    face.material_index=obj.data.materials.find(mat.name)


def body_side(b,s,articulated=False):
    # Four real pockets: only the outboard wall follows this lower contour.
    # The central hood and rear waist remain uninterrupted above them.
    samples=[(-4.0,.23),(-1.80,.23),(1.70,.23),(4.0,.23)]
    for axle in (WHEELS['rear_z'],WHEELS['front_z']):
        samples += [(axle-.49,.23),(axle-.49,.41)]
        samples += [(axle-.49*math.cos(i*math.pi/10),.41+.43*math.sin(i*math.pi/10)) for i in range(1,10)]
        samples += [(axle+.49,.41),(axle+.49,.23)]
    samples.sort(key=lambda p:p[0]);sections=[]
    for y,low in samples:
        taper=.05*max(0,(abs(y)-3.3)/.7)
        top=height(y)
        ring=[(.48,low),(.96-taper,low),(1.015-taper,low+.035),
              (1.03-taper,min(top-.08,low+.11)),(1.025-taper,top-.065),(.97-taper,top),(.48,top)]
        sections.append((y,ring))
    if articulated:
        fixed,moving=split_cabin_side(b,sections,s,DOOR,-2.20)
        objects=fixed+([moving] if moving else [])
        if moving:b.door_parts.append(moving)
    else:
        objects=[b.loft('IntegratedArchWall'+str(s),[(z,[(s*x,h) for x,h in r]) for z,r in sections],'SIDE')]
    for obj in objects:
        for face in obj.data.polygons:
            if face.normal.z<-.5:material_face(b,obj,face,'SHADOW')
            elif face.normal.z>.5 and min(obj.data.vertices[i].co.z for i in face.vertices)>.88:
                material_face(b,obj,face,'TOP')
            elif articulated and abs(face.normal.x)>.5 and all(abs(abs(obj.data.vertices[i].co.x)-DOOR['inner_x'])<1e-5 for i in face.vertices):
                material_face(b,obj,face,'BLACK')


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


def build_vehicle(articulated=False):
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    for material in list(bpy.data.materials):bpy.data.materials.remove(material)
    b=VehicleBuilder();b.door_parts=[]
    b.box('NarrowChassis',(-.40,-3.96,.23),(.40,3.96,.36),'SHADOW')
    for side in (-1,1):body_side(b,side,articulated)
    ranges=([('FormalHood',(1.60,1.70,2.85,4.0)),('RearDeck',(-4.0,-2.20))] if articulated
            else [('ContinuousHoodAndDeck',(-4.0,1.70,2.85,4.0))])
    for label,stations in ranges:
        b.loft(label,[(y,[(-.48,.88),(-.48,height(y)),(.48,height(y)),(.48,.88)]) for y in stations],'TOP')
    vertices=[(-.97,-2.72,1.07),(.97,-2.72,1.07),(-.97,1.70,1.07),(.97,1.70,1.07),
        (-.86,-2.26,1.64),(.86,-2.26,1.64),(-.86,1.22,1.64),(.86,1.22,1.64)]
    if not articulated:
        cabin=b.add_mesh('LongFormalPassengerCabin',vertices,[(0,2,3,1),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)],'TOP')
        for face in cabin.data.polygons:
            name={1:'BACK_GLASS',2:'WINDSHIELD',3:'CABIN',4:'CABIN'}.get(face.index)
            if name:material_face(b,cabin,face,name)
    else:
        b.add_mesh('FormalWindshield',vertices,[(2,6,7,3)],'WINDSHIELD')
        b.add_mesh('RearWindow',vertices,[(0,1,5,4)],'BACK_GLASS')
        b.add_mesh('PassengerUpperCabin',vertices,[(0,4,6,2)],'CABIN')
        def skin(name,points,moving=False):
            verts=points+[(x-.025,z,h) for x,z,h in points]
            obj=b.add_mesh(name,verts,[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],'CABIN')
            if moving:b.door_parts.append(obj)
        skin('PassengerInsertAndRearDoor',[(.97,-2.72,1.07),(.97,.18,1.07),(.86,.18,1.64),(.86,-2.26,1.64)])
        skin('DriverAPillar',[(.97,1.60,1.07),(.97,1.70,1.07),(.86,1.22,1.64),(.86,1.18,1.64)])
        skin('DriverUpperDoor',[(.97,.18,1.07),(.97,1.60,1.07),(.86,1.18,1.64),(.86,.18,1.64)],True)
        b.box('PassengerAndChauffeurFloor',(-.86,-2.20,.31),(.86,1.60,.35),'BLACK')
        b.box('RearBulkhead',(-.86,-2.22,.35),(.86,-2.15,1.07),'BLACK')
        b.box('FormalDashboard',(-.84,1.40,.82),(.84,1.60,1.02),'BLACK')
        b.box('CentreConsole',(-.10,.25,.35),(.10,1.35,.52),'SHADOW')
        for x in (-.46,.46):
            b.box('ChauffeurSeatCushion'+str(x),(x-.24,.38,.46),(x+.24,.95,.62),'CLADDING')
            b.box('ChauffeurSeatBack'+str(x),(x-.24,.20,.55),(x+.24,.38,1.23),'CLADDING')
        b.box('PassengerBenchCushion',(-.78,-2.03,.43),(.78,-1.43,.62),'CLADDING')
        b.box('PassengerBenchBack',(-.78,-2.14,.57),(.78,-2.02,1.24),'CLADDING')
        b.box('ChauffeurPartitionLower',(-.82,-.16,.35),(.82,-.09,.82),'BLACK')
        b.box('ChauffeurPartitionGlass',(-.82,-.15,.82),(.82,-.10,1.43),'CLADDING')
        b.box('RearRefreshmentConsole',(-.81,-1.23,.35),(-.48,-.38,.79),'BLACK')
        verts=[]
        for z in (1.185,1.215):
            for radius in (.13,.099):
                verts += [(.46+radius*math.cos(i*math.tau/8),z,1.06+radius*math.sin(i*math.tau/8)) for i in range(8)]
        faces=[]
        for i in range(8):
            j=(i+1)%8;faces += [(i,j,j+8,i+8),(i+16,i+24,j+24,j+16),(i,j,j+16,i+16),(i+8,i+24,j+24,j+8)]
        b.add_mesh('SteeringRim',verts,faces,'BLACK')
        beam(b,'SteeringSpoke',(.35,1.20,1.06),(.57,1.20,1.06),.023,'METAL')
        beam(b,'SteeringColumn',(.46,1.44,.92),(.46,1.20,1.06),.043,'BLACK')
        for x in (.36,.56):b.box('Pedal'+str(x),(x-.04,1.43,.355),(x+.04,1.51,.385),'METAL')
    roof=[(-.86,1.64),(-.83,1.665),(-.73,1.68),(.73,1.68),(.83,1.665),(.86,1.64)]
    b.loft('VinylRoof',[(-2.26,roof),(1.22,roof)],'CLADDING')
    b.box('GrilleReceiver',(-.48,3.93,.23),(.48,4.0,.94),'SIDE')
    b.box('TailPanel',(-.48,-4.0,.23),(.48,-3.93,1.07),'SIDE')
    for end in (-1,1):
        ring=[(-.94,.25),(-1.03,.30),(-1.03,.48),(-.98,.52),(.98,.52),(1.03,.48),(1.03,.30),(.94,.25)]
        b.loft('ChromeBumper'+str(end),[(end*4.045-.055,ring),(end*4.045+.055,ring)],'METAL')
    for side in (-1,1):
        lo,hi=sorted((side*.94,side*1.10))
        stem=b.box('MirrorStem'+str(side),(lo,1.32,1.12),(hi,1.40,1.18),'METAL')
        lo,hi=sorted((side*1.07,side*1.13))
        mirror=b.box('FormalMirror'+str(side),(lo,1.21,1.15),(hi,1.46,1.34),'CLADDING')
        if articulated and side==1:b.door_parts.extend([stem,mirror])
        for face in mirror.data.polygons:
            if face.normal.y<-.9:material_face(b,mirror,face,'METAL')
    b.box('HoodOrnamentStem',(-.018,3.75,.955),(.018,3.79,1.055),'METAL')
    for obj in b.objects:
        for face in obj.data.polygons:
            ys=[obj.data.vertices[i].co.y for i in face.vertices]
            if all(abs(y-4.0)<1e-5 for y in ys) and face.normal.y>.9:material_face(b,obj,face,'FRONT')
            elif all(abs(y+4.0)<1e-5 for y in ys) and face.normal.y<-.9:material_face(b,obj,face,'REAR')
        bm=bmesh.new();bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
        bmesh.ops.triangulate(bm,faces=list(bm.faces))
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7)
        bmesh.ops.triangulate(bm,faces=list(bm.faces))
        bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.calc_area()<1e-7],context='FACES')
        bm.to_mesh(obj.data);bm.free();obj.data.update();uvs(obj)
    if articulated:
        b.objects=[o for o in b.objects if o not in b.door_parts]
        body=b.join();body.name='BODY_OPEN';body.data.name='HalcyonSovereignOpenBody'
        b.objects=b.door_parts;door=b.join();door.name='DRIVER_DOOR';door.data.name='HalcyonSovereignDriverDoor'
    else:body=b.join();body.data.name='HalcyonSovereignBody'
    for side in (-1,1):
        for label,z in (('F',2.85),('R',-2.70)):
            empty=bpy.data.objects.new('WHEEL_'+label+str(side),None);empty.location=(side*.89,z,.41)
            bpy.context.collection.objects.link(empty)
    assert len([o for o in bpy.context.scene.objects if o.type=='MESH'])==(2 if articulated else 1)
    return (body,door) if articulated else body


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--mesh',type=Path,required=True);p.add_argument('--blend',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    body=build_vehicle();args.blend.parent.mkdir(parents=True,exist_ok=True)
    texture=Path(__file__).resolve().parents[1]/'assets/textures/vehicles/halcyon_sovereign/body.png'
    atlas=bpy.data.images.load(str(texture),check_existing=True)
    atlas.filepath=bpy.path.relpath(str(texture),start=str(args.blend.parent))
    for slot in body.material_slots:
        mat=slot.material;mat.use_nodes=True
        node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas;node.interpolation='Closest'
        shader=mat.node_tree.nodes.get('Principled BSDF');shader.inputs['Roughness'].default_value=1.
        mat.node_tree.links.new(node.outputs['Color'],shader.inputs['Base Color'])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print('HALCYON_SOVEREIGN',export_emesh(body,args.mesh,'halcyon_sovereign'))

    body_open,door=build_vehicle(articulated=True)
    print('HALCYON_SOVEREIGN_OPEN',export_emesh(body_open,args.mesh.with_name('body_open.emesh'),'halcyon_sovereign'))
    print('HALCYON_SOVEREIGN_DOOR',export_emesh(door,args.mesh.with_name('driver_door.emesh'),'halcyon_sovereign'))
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend.with_name('articulated.blend')))
