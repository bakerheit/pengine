#!/usr/bin/env python3
"""Author SCYTHE with continuous arch walls and single-layer optical surfaces."""
import argparse
import math
import sys
from pathlib import Path
import bmesh
import bpy

sys.path.insert(0,str(Path(__file__).resolve().parent))
from vesper_scythe_spec import ATLAS_SIZE, REGIONS, BOUNDS, WHEELS, DOOR, DRIVER
from vesper_vx91_blender import VehicleBuilder, export_emesh
from vehicle_door_builder import split_cabin_side
from vesper_mistral_blender import beam


def height(y):
    if y<-.7:return .89
    if y<.65:return .82
    return .76-(y-.65)*(.23/1.595)

def outer_height(y):
    return height(y)+max(0,.88-height(y))*max(0,1-min(abs(y-a) for a in (1.30,-1.25))/.65)

def material_face(builder,obj,face,name):
    mat=builder.material(name)
    if mat.name not in obj.data.materials: obj.data.materials.append(mat)
    face.material_index=obj.data.materials.find(mat.name)


def body_side(b,s,articulated=False):
    # Four real pockets: only the outboard wall follows this lower contour.
    # The central hood and rear waist remain uninterrupted above them.
    samples=[(-2.245,.18),(-.65,.18),(.65,.18),(2.245,.18)]
    for axle in (WHEELS['rear_z'],WHEELS['front_z']):
        samples += [(axle-.46,.18),(axle-.46,.36)]
        samples += [(axle-.46*math.cos(i*math.pi/10),.36+.40*math.sin(i*math.pi/10)) for i in range(1,10)]
        samples += [(axle+.46,.36),(axle+.46,.18)]
    samples.sort(key=lambda p:p[0])
    sections=[]
    for y,low in samples:
        taper=.14*max(0,(y-1.3)/.945)+.035*max(0,(-y-1.5)/.745)
        scoop=.085*max(0,1-abs(y+.53)/.36)
        top=outer_height(y)
        ring=[(.43,min(low,height(y)-.06)),(.95-taper,low),(.99-taper,low+.025),
              (.99-taper-scoop,min(top-.07,low+.08)),(1.02-taper,top-.06),
              (.92-taper,top),(.43,height(y))]
        sections.append((y,ring))
    if articulated:
        fixed,moving=split_cabin_side(b,sections,s,DOOR,-.76)
        objects=fixed+([moving] if moving else [])
        if moving:b.door_parts.append(moving)
    else:objects=[b.loft('IntegratedArchWall'+str(s),[(z,[(s*x,h) for x,h in r]) for z,r in sections],'SIDE')]
    for obj in objects:
        for face in obj.data.polygons:
            if face.normal.z<-.5:material_face(b,obj,face,'SHADOW')
            elif face.normal.z>.5 and min(obj.data.vertices[i].co.z for i in face.vertices)>.48:material_face(b,obj,face,'TOP')
            elif articulated and abs(face.normal.x)>.5 and all(abs(abs(obj.data.vertices[i].co.x)-DOOR['inner_x'])<1e-5 for i in face.vertices):material_face(b,obj,face,'BLACK')


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
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for material in list(bpy.data.materials):bpy.data.materials.remove(material)
    b=VehicleBuilder();b.door_parts=[]
    b.box('NarrowChassis',(-.38,-2.19,.18),(.38,2.19,.30),'SHADOW')
    for s in (-1,1):body_side(b,s,articulated)
    for label,stations in ([('Hood',(.78,1.3,2.245)),('EngineDeck',(-2.245,-.76))] if articulated else [('ContinuousWedgeDeck',(-2.245,-.7,.65,1.30,2.245))]):
        b.loft(label,[(y,[(-.43,.45),(-.43,height(y)),(.43,height(y)),(.43,.45)]) for y in stations],'TOP')
    vertices=[(-.88,-1.20,.89),(.88,-1.20,.89),(-.86,1.15,.688),(.86,1.15,.688),
              (-.62,-.63,1.13),(.62,-.63,1.13),(-.62,.12,1.13),(.62,.12,1.13)]
    if not articulated:
        cabin=b.add_mesh('FacetedExoticCanopy',vertices,[(0,2,3,1),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)],'TOP')
        for face in cabin.data.polygons:
            name={1:'BACK_GLASS',2:'WINDSHIELD',3:'CABIN',4:'CABIN'}.get(face.index)
            if name:material_face(b,cabin,face,name)
    else:
        b.add_mesh('CanopyFront',vertices,[(2,6,7,3)],'WINDSHIELD')
        b.add_mesh('CanopyRear',vertices,[(0,1,5,4)],'BACK_GLASS')
        b.add_mesh('PassengerUpperCabin',vertices,[(0,4,6,2)],'CABIN')
        def skin(name,points,moving=False):
            verts=points+[(x-.025,z,h) for x,z,h in points]
            obj=b.add_mesh(name,verts,[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],'CABIN')
            if moving:b.door_parts.append(obj)
        skin('DriverRearQuarterGlass',[(.88,-1.2,.89),(.87,-.54,.82),(.62,-.54,1.13),(.62,-.63,1.13)])
        skin('DriverAPillar',[(.87,.78,height(.78)),(.86,1.15,.688),(.62,.12,1.13),(.62,.06,1.13)])
        skin('DriverUpperDoor',[(.87,-.54,.82),(.87,.78,height(.78)),(.62,.06,1.13),(.62,-.54,1.13)],True)
        b.box('CabinFloor',(-.85,-.76,.205),(.85,.78,.24),'BLACK')
        b.box('RearBulkhead',(-.85,-.77,.24),(.85,-.71,.82),'BLACK')
        b.box('Dashboard',(-.82,.62,.54),(.82,.78,.70),'BLACK')
        b.box('CentreConsole',(-.09,-.68,.24),(.09,.60,.39),'SHADOW')
        for x in (-.43,.43):
            b.box('SeatCushion'+str(x),(x-.23,-.52,.29),(x+.23,.09,.39),'CLADDING')
            b.loft('ReclinedSeatBack'+str(x),[(-.70,[(x-.23,.38),(x-.23,.86),(x+.23,.86),(x+.23,.38)]),
                (-.52,[(x-.23,.32),(x-.23,.68),(x+.23,.68),(x+.23,.32)])],'CLADDING')
        verts=[]
        for z in (.135,.165):
            for radius in (.11,.081):
                verts += [(.43+radius*math.cos(i*math.tau/8),z,.72+radius*math.sin(i*math.tau/8)) for i in range(8)]
        faces=[]
        for i in range(8):
            j=(i+1)%8;faces += [(i,j,j+8,i+8),(i+16,i+24,j+24,j+16),(i,j,j+16,i+16),(i+8,i+24,j+24,j+8)]
        b.add_mesh('SteeringRim',verts,faces,'BLACK')
        beam(b,'SteeringSpoke',(.34,.15,.72),(.52,.15,.72),.022,'METAL')
        beam(b,'SteeringColumn',(.43,.65,.61),(.43,.15,.72),.038,'BLACK')
        for x in (.33,.53):b.box('Pedal'+str(x),(x-.035,.51,.245),(x+.035,.59,.27),'METAL')
        b.box('GearLever',(-.018,.10,.39),(.018,.14,.51),'METAL')
    roof=[(-.62,1.13),(-.59,1.145),(-.50,1.155),(.50,1.155),(.59,1.145),(.62,1.13)]
    b.loft('CanopyRoof',[(-.63,roof),(.12,roof)],'TOP')
    b.box('FrontFascia',(-.43,2.17,.18),(.43,2.245,.53),'SIDE')
    b.box('RearFascia',(-.43,-2.245,.18),(.43,-2.17,.89),'SIDE')
    for end in (-1,1):
        half=.87 if end>0 else .98
        ring=[(-half+.05,.18),(-half,.205),(-half,.28),(-half+.035,.315),
              (half-.035,.315),(half,.28),(half,.205),(half-.05,.18)]
        b.loft('IntegratedValance'+str(end),[(end*2.285-.04,ring),(end*2.285+.04,ring)],'CLADDING')
    for side in (-1,1):
        lo,hi=sorted((side*.82,side*1.045))
        stem=b.box('MirrorStem'+str(side),(lo,.63,.88),(hi,.70,.925),'BLACK')
        if articulated and side==1:b.door_parts.append(stem)
        lo,hi=sorted((side*1.02,side*1.09))
        mirror=b.box('WedgeMirror'+str(side),(lo,.50,.90),(hi,.75,1.015),'BLACK')
        if articulated and side==1:b.door_parts.append(mirror)
        for face in mirror.data.polygons:
            if face.normal.y<-.9:material_face(b,mirror,face,'METAL')
        lo,hi=sorted((side*.66,side*.72))
        b.box('WingUpright'+str(side),(lo,-2.04,.89),(hi,-1.80,1.13),'BLACK')
    wing=[(-1.025,1.115),(-1.025,1.17),(1.025,1.17),(1.025,1.115)]
    b.loft('TransverseRearWing',[(-2.13,wing),(-1.79,wing)],'TOP')
    for side in (-1,1):
        lo,hi=sorted((side*1.015,side*1.05))
        b.box('WingEndplate'+str(side),(lo,-2.14,1.095),(hi,-1.78,1.19),'TOP')
    for obj in b.objects:
        for face in obj.data.polygons:
            ys=[obj.data.vertices[i].co.y for i in face.vertices]
            if all(abs(y-2.245)<1e-5 for y in ys) and face.normal.y>.9:
                material_face(b,obj,face,'FRONT')
            elif all(abs(y+2.245)<1e-5 for y in ys) and face.normal.y<-.9:
                material_face(b,obj,face,'REAR')
        bm=bmesh.new();bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
        bmesh.ops.triangulate(bm,faces=list(bm.faces))
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7)
        bmesh.ops.triangulate(bm,faces=list(bm.faces))
        bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.calc_area()<1e-7],context='FACES')
        bm.to_mesh(obj.data);bm.free();obj.data.update()
        uvs(obj)
    if articulated:
        b.objects=[o for o in b.objects if o not in b.door_parts]
        body=b.join();body.name='BODY_OPEN';body.data.name='VesperScytheOpenBody'
        b.objects=b.door_parts;door=b.join();door.name='DRIVER_DOOR';door.data.name='VesperScytheDriverDoor'
    else:
        body=b.join();body.data.name='VesperScytheBody'
    for s in (-1,1):
        for label,z in (('F',1.30),('R',-1.25)):
            empty=bpy.data.objects.new('WHEEL_'+label+('L' if s<0 else 'R'),None)
            empty.location=(s*.87,z,.36)
            bpy.context.collection.objects.link(empty)
    assert len([o for o in bpy.context.scene.objects if o.type=='MESH'])==(2 if articulated else 1)
    return (body,door) if articulated else body


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--mesh',type=Path,required=True);p.add_argument('--blend',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    body=build_vehicle();args.blend.parent.mkdir(parents=True,exist_ok=True)
    texture=Path(__file__).resolve().parents[1]/'assets/textures/vehicles/vesper_scythe/body.png'
    atlas=bpy.data.images.load(str(texture),check_existing=True)
    atlas.filepath=bpy.path.relpath(str(texture),start=str(args.blend.parent))
    for slot in body.material_slots:
        mat=slot.material;mat.use_nodes=True
        node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas;node.interpolation='Closest'
        shader=mat.node_tree.nodes.get('Principled BSDF');shader.inputs['Roughness'].default_value=1.
        mat.node_tree.links.new(node.outputs['Color'],shader.inputs['Base Color'])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print('VESPER_SCYTHE',export_emesh(body,args.mesh,'vesper_scythe'))

    body_open,door=build_vehicle(articulated=True)
    print('VESPER_SCYTHE_OPEN',export_emesh(body_open,args.mesh.with_name('body_open.emesh'),'vesper_scythe'))
    print('VESPER_SCYTHE_DOOR',export_emesh(door,args.mesh.with_name('driver_door.emesh'),'vesper_scythe'))
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend.with_name('articulated.blend')))
