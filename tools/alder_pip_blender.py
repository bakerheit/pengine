#!/usr/bin/env python3
"""Author PIP with continuous arch walls and single-layer optical surfaces."""
import argparse
import math
import sys
from pathlib import Path
import bmesh
import bpy

sys.path.insert(0,str(Path(__file__).resolve().parent))
from alder_pip_spec import ATLAS_SIZE, REGIONS, BOUNDS, WHEELS, DOOR, DRIVER
from vesper_vx91_blender import VehicleBuilder, export_emesh
from vehicle_door_builder import split_cabin_side
from vesper_mistral_blender import beam


def height(y):
    return .88 if y<=.65 else .88-(y-.65)*(.08/1.17)


def material_face(builder,obj,face,name):
    mat=builder.material(name)
    if mat.name not in obj.data.materials: obj.data.materials.append(mat)
    face.material_index=obj.data.materials.find(mat.name)


def body_side(b,s,articulated=False):
    # Four real pockets: only the outboard wall follows this lower contour.
    # The central hood and rear waist remain uninterrupted above them.
    samples=[(-1.82,.19),(-.55,.19),(.65,.19),(1.82,.19)]
    for axle in (WHEELS['rear_z'],WHEELS['front_z']):
        samples += [(axle-.43,.19),(axle-.43,.34)]
        samples += [(axle-.43*math.cos(i*math.pi/10),.34+.39*math.sin(i*math.pi/10)) for i in range(1,10)]
        samples += [(axle+.43,.34),(axle+.43,.19)]
    # .65 is outside the front arch; explicit stations stay below the opening.
    samples.sort(key=lambda p:p[0])
    sections=[]
    for y,low in samples:
        taper=.04*max(0,(abs(y)-1.5)/.32)
        flare=.025*max(0,1-min(abs(y-a) for a in (1.10,-1.26))/.56)
        top=height(y)
        ring=[(.38,low),(.80+flare-taper,low),(.835+flare-taper,low+.03),
              (.835+flare-taper,min(top-.06,low+.09)),(.83-taper,top-.055),(.80-taper,top),(.38,top)]
        sections.append((y,ring))
    if articulated:
        fixed,moving=split_cabin_side(b,sections,s,DOOR,-.80)
        objects=fixed+([moving] if moving else [])
        if moving:b.door_parts.append(moving)
    else:
        objects=[b.loft('IntegratedArchWall'+str(s),[(z,[(s*x,h) for x,h in r]) for z,r in sections],'SIDE')]
    for obj in objects:
        for face in obj.data.polygons:
            if face.normal.z<-.5:material_face(b,obj,face,'SHADOW')
            elif face.normal.z>.5 and min(obj.data.vertices[i].co.z for i in face.vertices)>.74:
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


def glazed_frame(b,name,outer,inner,thickness,material,moving=False):
    """Capped opaque rim, with a separate single-layer optical pane."""
    points=outer+inner
    points += [tuple(p[i]+thickness[i] for i in range(3)) for p in points]
    faces=[]
    for i in range(4):
        j=(i+1)%4
        faces += [(i,j,j+4,i+4),(i+8,i+12,j+12,j+8),
                  (i,i+8,j+8,j),(i+4,j+4,j+12,i+12)]
    frame=b.add_mesh(name+'Frame',points,faces,material)
    if moving:b.door_parts.append(frame)
    pane=b.add_mesh(name,inner,[(0,1,2,3)],'BLACK')
    b.glass_parts[name]=pane


def build_vehicle(articulated=False):
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    for material in list(bpy.data.materials):bpy.data.materials.remove(material)
    b=VehicleBuilder();b.door_parts=[];b.glass_parts={}
    b.box('NarrowChassis',(-.32,-1.77,.19),(.32,1.77,.32),'SHADOW')
    for s in (-1,1): body_side(b,s,articulated)
    for label,stations in ([('Hood',(.58,.65,1.10,1.82)),('RearWaist',(-1.82,-.80))] if articulated else [('ContinuousHoodAndWaist',(-1.82,.65,1.10,1.82))]):
        b.loft(label,[(y,[(-.38,.74),(-.38,height(y)),(.38,height(y)),(.38,.74)]) for y in stations],'TOP')
    vertices=[(-.80,-1.82,.88),(.80,-1.82,.88),(-.80,.65,.88),(.80,.65,.88),
              (-.69,-1.58,1.48),(.69,-1.58,1.48),(-.69,.18,1.48),(.69,.18,1.48)]
    if not articulated:
        cabin=b.add_mesh('ThreeDoorHatchCabin',vertices,
                        [(0,2,3,1),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)],'TOP')
        for face in cabin.data.polygons:
            name={1:'BACK_GLASS',2:'WINDSHIELD',3:'CABIN',4:'CABIN'}.get(face.index)
            if name:material_face(b,cabin,face,name)
    else:
        for name,z0,z1,material,depth in (
                ('windshield',.65,.18,'WINDSHIELD',-.025),
                ('rear_glass',-1.82,-1.58,'BACK_GLASS',.025)):
            def end_point(x,h):return (x,z0+(h-.88)/.60*(z1-z0),h)
            glazed_frame(b,name,[end_point(x,h) for x,h in
                ((-.80,.88),(.80,.88),(.69,1.48),(-.69,1.48))],
                [end_point(x,h) for x,h in
                ((-.66,.98),(.66,.98),(.59,1.38),(-.59,1.38))],
                (0,depth,0),material)
        def skin(name,points,moving=False):
            # Thin capped skin remains visible from inside the open cabin.
            verts=points+[(x-math.copysign(.025,x),z,h) for x,z,h in points]
            obj=b.add_mesh(name,verts,[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],'CABIN')
            if moving:b.door_parts.append(obj)
        for side in (-1,1):
            def side_point(z,h):return (side*(.80-(h-.88)*.11/.60),z,h)
            for name,outer,inner in (
                    ('driver_rear_glass' if side==1 else 'passenger_rear_glass',
                     ((-1.82,.88),(-.55,.88),(-.55,1.48),(-1.58,1.48)),
                     ((-1.60,.98),(-.66,.98),(-.66,1.36),(-1.44,1.36))),
                    ('driver_glass' if side==1 else 'passenger_glass',
                     ((-.55,.88),(.58,.88),(.14,1.48),(-.55,1.48)),
                     ((-.45,.98),(.43,.98),(.10,1.36),(-.45,1.36)))):
                glazed_frame(b,name,[side_point(z,h) for z,h in outer],
                    [side_point(z,h) for z,h in inner],(-side*.025,0,0),'CABIN',
                    moving=name=='driver_glass')
        # The passenger A-pillar also needs an inward-facing capped shell.
        skin('PassengerAPillar',[(-.80,.65,.88),(-.80,.58,.88),(-.69,.14,1.48),(-.69,.18,1.48)])
        skin('DriverAPillar',[(.80,.58,.88),(.80,.65,.88),(.69,.18,1.48),(.69,.14,1.48)])
        b.box('CabinFloor',(-.73,-.80,.25),(.73,.58,.29),'BLACK')
        b.box('RearBulkhead',(-.73,-.80,.29),(.73,-.74,.88),'BLACK')
        b.box('Dashboard',(-.72,.42,.70),(.72,.58,.87),'BLACK')
        b.box('CentreConsole',(-.075,-.67,.29),(.075,.40,.46),'SHADOW')
        for x in (-.38,.38):
            b.box('SeatCushion'+str(x),(x-.22,-.53,.38),(x+.22,.04,.53),'CLADDING')
            b.box('SeatBack'+str(x),(x-.22,-.64,.48),(x+.22,-.53,1.10),'CLADDING')
            b.box('HeadRest'+str(x),(x-.16,-.635,1.10),(x+.16,-.535,1.24),'CLADDING')
        verts=[]
        for z in (.245,.275):
            for radius in (.115,.086):
                verts += [(.38+radius*math.cos(i*math.tau/8),z,.92+radius*math.sin(i*math.tau/8)) for i in range(8)]
        faces=[]
        for i in range(8):
            j=(i+1)%8;faces += [(i,j,j+8,i+8),(i+16,i+24,j+24,j+16),(i,j,j+16,i+16),(i+8,i+24,j+24,j+8)]
        b.add_mesh('SteeringRim',verts,faces,'BLACK')
        beam(b,'SteeringSpoke',(.28,.26,.92),(.48,.26,.92),.022,'METAL')
        beam(b,'SteeringColumn',(.38,.44,.80),(.38,.26,.92),.040,'BLACK')
        for x in (.29,.47):b.box('Pedal'+str(x),(x-.035,.43,.295),(x+.035,.51,.32),'METAL')
        b.box('GearLever',(-.018,.04,.46),(.018,.08,.59),'METAL')
    roof=[(-.69,1.48),(-.66,1.507),(-.58,1.52),(.58,1.52),(.66,1.507),(.69,1.48)]
    b.loft('PressedSteelRoof',[(-1.58,roof),(.18,roof)],'TOP')
    b.box('FrontFascia',(-.38,1.75,.19),(.38,1.82,.74),'SIDE')
    b.box('HatchLowerPanel',(-.38,-1.82,.19),(.38,-1.75,.74),'SIDE')
    for end in (-1,1):
        ring=[(-.76,.22),(-.85,.25),(-.85,.39),(-.80,.43),(.80,.43),(.85,.39),(.85,.25),(.76,.22)]
        b.loft('SlimImpactBumper'+str(end),[(end*1.855-.045,ring),(end*1.855+.045,ring)],'CLADDING')
    for s in (-1,1):
        lo,hi=sorted((s*.78,s*.94))
        stem=b.box('MirrorStem'+str(s),(lo,.42,.99),(hi,.49,1.04),'BLACK')
        if articulated and s==1:b.door_parts.append(stem)
        lo,hi=sorted((s*.91,s*.97))
        mirror=b.box('CompactMirror'+str(s),(lo,.31,1.01),(hi,.52,1.17),'BLACK')
        if articulated and s==1:b.door_parts.append(mirror)
        for face in mirror.data.polygons:
            if face.normal.y<-.9:material_face(b,mirror,face,'METAL')
    for obj in b.objects:
        for face in obj.data.polygons:
            ys=[obj.data.vertices[i].co.y for i in face.vertices]
            if all(abs(y-1.82)<1e-5 for y in ys) and face.normal.y>.9:
                material_face(b,obj,face,'FRONT')
            elif all(abs(y+1.82)<1e-5 for y in ys) and face.normal.y<-.9:
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
        b.objects=[o for o in b.objects if o not in b.door_parts and o not in b.glass_parts.values()]
        body=b.join();body.name='BODY_OPEN';body.data.name='AlderPipOpenBody'
        b.objects=b.door_parts;door=b.join();door.name='DRIVER_DOOR';door.data.name='AlderPipDriverDoor'
    else:
        body=b.join();body.data.name='AlderPipBody'
    for s in (-1,1):
        for label,z in (('F',1.10),('R',-1.26)):
            empty=bpy.data.objects.new('WHEEL_'+label+('L' if s<0 else 'R'),None)
            empty.location=(s*.72,z,.34)
            bpy.context.collection.objects.link(empty)
    assert len([o for o in bpy.context.scene.objects if o.type=='MESH'])==(8 if articulated else 1)
    return (body,door,b.glass_parts) if articulated else body


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--mesh',type=Path,required=True);p.add_argument('--blend',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    body=build_vehicle();args.blend.parent.mkdir(parents=True,exist_ok=True)
    texture=Path(__file__).resolve().parents[1]/'assets/textures/vehicles/alder_pip/body.png'
    atlas=bpy.data.images.load(str(texture),check_existing=True)
    atlas.filepath=bpy.path.relpath(str(texture),start=str(args.blend.parent))
    for slot in body.material_slots:
        mat=slot.material;mat.use_nodes=True
        node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas;node.interpolation='Closest'
        shader=mat.node_tree.nodes.get('Principled BSDF');shader.inputs['Roughness'].default_value=1.
        mat.node_tree.links.new(node.outputs['Color'],shader.inputs['Base Color'])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print('ALDER_PIP',export_emesh(body,args.mesh,'alder_pip'))

    body_open,door,glass=build_vehicle(articulated=True)
    print('ALDER_PIP_OPEN',export_emesh(body_open,args.mesh.with_name('body_open.emesh'),'alder_pip'))
    print('ALDER_PIP_DOOR',export_emesh(door,args.mesh.with_name('driver_door.emesh'),'alder_pip'))
    for name,pane in glass.items():
        print('ALDER_PIP_GLASS',name,export_emesh(pane,args.mesh.with_name(name+'.emesh'),'alder_pip'))
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend.with_name('articulated.blend')))
