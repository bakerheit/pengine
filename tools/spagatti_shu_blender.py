#!/usr/bin/env python3
"""Connected early-PS2 Spagatti Shū. Blender +X/driver left, +Y/forward, +Z/up."""
from __future__ import annotations
import argparse
import json
import math
import sys
import struct
from pathlib import Path
import bmesh
import bpy
from mathutils import Vector
sys.path.insert(0, str(Path(__file__).resolve().parent))
from spagatti_shu_spec import ATLAS_SIZE, REGIONS, UV_PROJECTIONS, WHEEL_ANCHORS, PLATE_MOUNTS, SHAPE, LAMPS
from vesper_vx91_blender import VehicleBuilder, export_emesh
from rodeo_grazer_cab import Sheet


def assign_uvs(obj):
    mesh = obj.data
    layer = mesh.uv_layers.new(name='UVMap')
    lamp_points={cx:[] for cx,_ in LAMPS['headlights']['centers']}
    for face in mesh.polygons:
        if obj.material_slots[face.material_index].material.name.split('.')[0]=='HEADLIGHT':
            cx=min(lamp_points,key=lambda x:abs(face.center.x-x))
            lamp_points[cx].extend(mesh.vertices[v].co for v in face.vertices)
    for face in mesh.polygons:
        key = obj.material_slots[face.material_index].material.name.split('.')[0]
        x0,y0,x1,y1 = REGIONS[key]
        points = [mesh.vertices[mesh.loops[i].vertex_index].co for i in face.loop_indices]
        if key in UV_PROJECTIONS:
            axes, bounds = UV_PROJECTIONS[key]
        elif key=='HEADLIGHT':
            # All triangles in a curved lens share one receiver. Mapping each
            # triangle separately repeats the pupil and breaks the reflector.
            cx=min(lamp_points,key=lambda x:abs(face.center.x-x));axes=(0,2)
            bounds=[(min(p[a] for p in lamp_points[cx]),max(p[a] for p in lamp_points[cx])) for a in axes]
        else:
            n=face.normal; axes=(0,1) if abs(n.z)>max(abs(n.x),abs(n.y)) else ((0,2) if abs(n.y)>abs(n.x) else (1,2))
            bounds=[(min(p[a] for p in points),max(p[a] for p in points)) for a in axes]
        for loop,p in zip(face.loop_indices,points):
            values=[.5 if hi-lo<1e-7 else (p[a]-lo)/(hi-lo) for a,(lo,hi) in zip(axes,bounds)]
            u,v=[max(0,min(1,x)) for x in values]
            layer.data[loop].uv=((x0+2+u*(x1-x0-4))/256,1-(y1-2-v*(y1-y0-4))/256)


def boolean_cut(obj, cutter):
    bpy.context.view_layer.objects.active=obj
    mod=obj.modifiers.new('Real opening','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter,do_unlink=True)


def well_cutter(b, side, y):
    # Four local side pockets, never a tunnel through the center hood/deck.
    vs=[]; n=12
    for edge in range(2):
        for i in range(n):
            z=.43+.495*math.sin(math.tau*i/n)
            x=.52+max(0,z-.72)*1.95 if edge==0 else 1.4
            vs.append((side*x,y+.54*math.cos(math.tau*i/n),z))
    # Triangulate the inboard cap in horizontal bands. A single warped ngon
    # would diagonally bridge the steering pocket and punch through the hood.
    fs=[(3,4,2),(2,4,5,1),(1,5,6,0),(0,6,7,11),(11,7,8,10),(10,8,9),tuple(range(n,2*n))]
    fs.extend((i,(i+1)%n,n+(i+1)%n,n+i) for i in range(n))
    obj=b.add_mesh('Temporary wheel cutter',vs,fs,'BLACK');b.objects.remove(obj)
    return obj


def ring(width, shoulder, top):
    crown=top+(shoulder-top)*.53+.018
    return [(-width*.62,.16),(-width*.92,.24),(-width,.42),
            (-width,shoulder-.11),(-width*.88,shoulder),(-width*.71,crown),(-width*.52,top),
            (width*.52,top),(width*.71,crown),(width*.88,shoulder),(width,shoulder-.11),
            (width,.42),(width*.92,.24),(width*.62,.16)]


def inset(points, distance):
    center=sum((Vector(p) for p in points),Vector())/len(points)
    return [tuple(Vector(p)+(center-Vector(p)).normalized()*distance) for p in points]


def glass_object(b, name, points, desired):
    points=inset(points,-.007) # Lap beneath the inner frame by seven millimeters.
    normal=Vector(desired).normalized()
    points=[tuple(Vector(p)-normal*.012) for p in points]
    key='GLASS_FRONT' if name=='windshield' else ('GLASS_REAR' if name=='rear_glass' else 'GLASS_SIDE')
    if name in ('windshield','rear_glass'):
        # Matched vertical strips follow the curved upper/lower edges. A single
        # warped ngon can triangulate its two skins differently and self-occlude.
        points=points[:5]+list(reversed(points[5:]))
        faces=[(i,i+1,i+6,i+5) for i in range(4)]
        obj=b.add_mesh(name,points,faces,key)
        if obj.data.polygons[0].normal.dot(normal)<0:
            bm=bmesh.new();bm.from_mesh(obj.data)
            bmesh.ops.reverse_faces(bm,faces=list(bm.faces));bm.to_mesh(obj.data);bm.free()
    else:
        obj=b.panel(name,points,key,desired)
    b.objects.remove(obj)
    # Thin closed panes give predictable exterior and interior normals.
    bpy.context.view_layer.objects.active=obj
    mod=obj.modifiers.new('Thin glazing','SOLIDIFY');mod.thickness=.004;mod.offset=-1
    bpy.ops.object.modifier_apply(modifier=mod.name)
    for face in obj.data.polygons:face.use_smooth=abs(face.normal.dot(normal))>.7
    return obj


def finish_frame(sheet,b):
    """Weld station boundaries before adding the inward window returns."""
    points=[Vector(v) for v in sheet.vertices];faces=[]
    for face in sheet.faces:
        split=[]
        for i,a in enumerate(face):
            z=face[(i+1)%len(face)];delta=points[z]-points[a];split.append(a)
            middle=[]
            for k,p in enumerate(points):
                t=(p-points[a]).dot(delta)/delta.length_squared
                if 1e-6<t<1-1e-6 and (p-points[a]-t*delta).length<1e-6:middle.append((t,k))
            split.extend(k for _,k in sorted(middle))
        faces.append(split)
    obj=b.add_mesh('Connected roof and window surrounds',sheet.vertices,faces,'BODY_TOP')
    obj.data.materials.append(b.material('BODY_SIDE'));obj.data.materials.append(b.material('BODY_SHADOW'))
    for f,key in zip(obj.data.polygons,sheet.keys):f.material_index=['BODY_TOP','BODY_SIDE','BODY_SHADOW'].index(key)
    bpy.context.view_layer.objects.active=obj
    mod=obj.modifiers.new('Inward frame thickness','SOLIDIFY');mod.thickness=.012;mod.offset=-1
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bm=bmesh.new();bm.from_mesh(obj.data)
    assert all(e.is_manifold for e in bm.edges),'Window surround contains an open structural edge'
    todo=set(bm.verts);stack=[next(iter(todo))]
    while stack:
        v=stack.pop()
        if v in todo:todo.remove(v);stack.extend(e.other_vert(v) for e in v.link_edges)
    assert not todo,'Disconnected roof/pillar/window frame'
    bm.free();obj['continuous_shell']=True
    for f in obj.data.polygons:f.use_smooth=True
    return obj


def canopy(b):
    sheet=Sheet(); panes={}
    rows=[(-.48,.56,1.145),(-.25,.61,1.165),(.05,.61,1.170),(.31,.57,1.145)]
    roof=[]
    for y,w,h in rows:
        roof.append([(w*q,y,h+math.sqrt(max(0,1-q*q))*.055) for q in (-1,-.75,0,.75,1)])
    for a,c in zip(roof,roof[1:]):
        for i in range(4):sheet.face([a[i],a[i+1],c[i+1],c[i]],'BODY_TOP')
    front=[(-.73,.86,.82),(-.52,.88,.845),(0,.895,.855),(.52,.88,.845),(.73,.86,.82),*reversed(roof[-1])]
    rear=[(.73,-1.10,.855),(.52,-1.12,.88),(0,-1.13,.89),(-.52,-1.12,.88),(-.73,-1.10,.855),*roof[0]]
    for name,outer,normal in [('windshield',front,(0,1,.7)),('rear_glass',rear,(0,-1,.8))]:
        inner=inset(outer,.045);sheet.ring(outer,inner,'BODY_TOP')
        panes[name]=glass_object(b,name,inner,normal)
    for s in (-1,1):
        prefix='driver' if s>0 else 'passenger'
        outer=[(s*.73,.86,.82),(s*.73,-1.10,.855),*[(s*w,y,h) for y,w,h in rows]]
        inner=inset(outer,.042)
        sheet.ring(outer,inner,'BODY_SIDE')
        # One narrow rear quarter divider; no disconnected pillar beams.
        low=Vector(inner[1]).lerp(Vector(inner[0]),.28)
        high=Vector(inner[2]).lerp(Vector(inner[3]),.40)
        a=tuple(Vector(inner[1]).lerp(Vector(inner[0]),.289));aa=tuple(Vector(inner[1]).lerp(Vector(inner[0]),.271))
        c=tuple(Vector(inner[2]).lerp(Vector(inner[3]),.45));cc=tuple(Vector(inner[2]).lerp(Vector(inner[3]),.35))
        sheet.face([a,aa,cc,c],'BODY_SHADOW')
        panes[prefix+'_glass']=glass_object(b,prefix+'_glass',[inner[0],a,c,*inner[3:]],(s,0,.3))
        panes[prefix+'_rear_glass']=glass_object(b,prefix+'_rear_glass',[aa,inner[1],inner[2],cc],(s,0,.3))
    # Frame thickness is inward. No independent bevels to pull joins apart.
    obj=finish_frame(sheet,b)
    return panes


def recess(b,name,outer,normal,depth,trim='BODY_SIDE',lens='BLACK'):
    normal=Vector(normal).normalized()
    inner=inset(outer,.018)
    back=[tuple(Vector(p)-normal*depth) for p in inner]
    vertices=outer+inner+back;n=len(outer)
    faces=[];keys=[]
    for a,c,k in [(0,n,trim),(n,2*n,'LENS_DARK')]:
        for i in range(n):
            j=(i+1)%n;faces.append((a+i,a+j,c+j,c+i));keys.append(k)
    faces.append(tuple(range(2*n,3*n)));keys.append(lens)
    obj=b.add_mesh(name,vertices,faces,trim)
    for key in dict.fromkeys(keys):
        if key!=trim:obj.data.materials.append(b.material(key))
    for poly,key in zip(obj.data.polygons,keys):poly.material_index=list(m.name for m in obj.data.materials).index(key)
    return obj


def headlamp(b,shell,side,x,z):
    """Eight-sided bucket cut into the measured curved fender receiver."""
    outer=[]
    for i in range(8):
        angle=math.tau*i/8
        px=side*x+.100*math.cos(angle);pz=z+.100*math.sin(angle)
        hit,loc,_,_=shell.ray_cast(Vector((px,3,pz)),Vector((0,-1,0)))
        assert hit and loc.y>1.8,('headlamp receiver missing',px,pz)
        outer.append(tuple(loc+Vector((0,.002,0))))
    # Planar caps span the complete receiver. A cap following the warped skin
    # can fold through the fender and leave the outer lens under red paint.
    rear=min(p[1] for p in outer)-.070
    cutter=b.add_mesh('Temporary round lamp cutter',[(px,3,pz) for px,_,pz in outer]+[(px,rear,pz) for px,_,pz in outer],
        [tuple(range(8)),tuple(reversed(range(8,16)))]+[(i,(i+1)%8,8+(i+1)%8,8+i) for i in range(8)],'BLACK')
    b.objects.remove(cutter);boolean_cut(shell,cutter)
    back=[tuple(Vector(p)+Vector((0,-.024,0))) for p in inset(outer,.016)]
    obj=b.add_mesh('Recessed round headlamp',outer+back,
        [(i,(i+1)%8,8+(i+1)%8,8+i) for i in range(8)]+[tuple(range(8,16))],'METAL')
    obj.data.materials.append(b.material('HEADLIGHT'))
    obj.data.polygons[-1].material_index=1


def round_nose(obj):
    """Wrap the complete front stamping and its fitted recesses together."""
    bm=bmesh.new();bm.from_mesh(obj.data)
    if obj.name.startswith('Continuous sculpted body'):
        # Re-tessellating the curved skin during later booleans can leave tiny
        # triangular slivers at the cabin cut. Close only those small seams.
        boundary=[e for e in bm.edges if e.is_boundary]
        if boundary:
            fills=bmesh.ops.holes_fill(bm,edges=boundary,sides=3)['faces']
            for face in fills:
                assert face.calc_area()<.001,'Unexpected large opening in the body'
                neighbor=next(f for e in face.edges for f in e.link_faces if f!=face)
                face.material_index=neighbor.material_index;face.smooth=neighbor.smooth
    # The boolean openings already supply useful fascia vertices. Triangulate
    # before wrapping so a warped cap cannot choose a different diagonal later.
    bmesh.ops.triangulate(bm,faces=[f for f in bm.faces if any(v.co.y>1.90 for v in f.verts)])
    for vertex in bm.verts:
        x,y,z=vertex.co
        if y<=.90:continue
        t=max(0,min(1,(y-1.85)/.55))
        blend=t*t*(3-2*t)
        setback=.30*(abs(x)/1.07)**2+.14*((z-.48)/.38)**2
        # A small stamped landing keeps the registration rigid and flush.
        px=max(0,(abs(x-.52)-.185)/.055)
        pz=max(0,(abs(z-.33)-.108)/.045)
        landing=max(0,1-max(px,pz));landing=landing*landing*(3-2*landing)
        setback=setback*(1-landing)+.098*landing
        vertex.co.y-=blend*(setback-.015)
        # Long, shallow hood crown; zero at the cowl and nose edge.
        hood=max(0,math.sin(math.pi*min(1,(y-.90)/1.50)))
        upper=max(0,min(1,(z-.65)/.15))
        center_weight=max(.12,1-.90*(abs(x)/1.07)**2)
        vertex.co.z+=.140*hood*upper*center_weight
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(obj.data);bm.free()
    for face in obj.data.polygons:
        material=obj.data.materials[face.material_index].name
        if face.center.y>1.90 and face.center.z>.20 and material.startswith('BODY_'):
            face.use_smooth=True


def check_shell(shell):
    bm=bmesh.new();bm.from_mesh(shell.data)
    assert all(e.is_manifold for e in bm.edges),'Open main body shell'
    bm.free()
    root=Path(__file__).resolve().parents[1]
    raw=(root/'assets/models/vehicles/common/wheel.emesh').read_bytes()
    count=struct.unpack_from('<8I',raw)[3]
    pts=[struct.unpack_from('<3f',raw,32+48*i) for i in range(count)]
    ni=struct.unpack_from('<8I',raw)[4];idx=struct.unpack_from('<%dI'%ni,raw,32+48*count)
    for k in range(0,ni,3):
        a,c,d=[Vector(pts[i]) for i in idx[k:k+3]]
        pts.extend(tuple(v) for v in ((a+c)/2,(c+d)/2,(d+a)/2,(a+c+d)/3))
    radius=max(max(p[1] for p in pts)-min(p[1] for p in pts),max(p[2] for p in pts)-min(p[2] for p in pts))*.5
    points={tuple(round(v*.43/radius,6) for v in (p[0],p[2],p[1])) for p in pts}
    intrusions=[]
    for side in (-1,1):
        for axle in (1.45,-1.35):
            for steer in (-.60,-.30,0,.30,.60) if axle>0 else (0,):
                c=math.cos(steer);q=math.sin(steer)
                for x,y,z in points:
                    p=Vector((side*.94+x*c-y*q,axle+x*q+y*c,.43+z))
                    hit,loc,n,_=shell.ray_cast(p,Vector((side,0,0)))
                    if hit and n.x*side>.1 and (loc-p).length>.005:intrusions.append((tuple(p),steer))
    return {'wheel_samples':len(points)*12,'wheel_intrusions':len(intrusions),'examples':intrusions[:4]}


def source_materials(body,panes,texture_path):
    image=bpy.data.images.load(str(texture_path),check_existing=True)
    for mat in body.data.materials:
        mat.use_nodes=True;nodes=mat.node_tree.nodes;shader=nodes.get('Principled BSDF')
        tex=nodes.new('ShaderNodeTexImage');tex.image=image;tex.interpolation='Closest'
        mat.node_tree.links.new(tex.outputs['Color'],shader.inputs['Base Color'])
        shader.inputs['Roughness'].default_value=.42 if mat.name.startswith('BODY_') else .85
        shader.inputs['Specular IOR Level'].default_value=.25
    for obj in panes.values():
        for mat in obj.data.materials:
            mat.use_nodes=True;shader=mat.node_tree.nodes.get('Principled BSDF')
            shader.inputs['Base Color'].default_value=(.24,.32,.35,1)
            shader.inputs['Alpha'].default_value=.30;shader.inputs['Roughness'].default_value=.14
            mat.surface_render_method='DITHERED'
    # Native source displays the exact shared wheels, separate from all exports.
    root=Path(__file__).resolve().parents[1];raw=(root/'assets/models/vehicles/common/wheel.emesh').read_bytes()
    _,_,_,nv,ni,*_=struct.unpack_from('<8I',raw)
    vertices=[struct.unpack_from('<12f',raw,32+48*i) for i in range(nv)]
    ii=struct.unpack_from('<%dI'%ni,raw,32+48*nv)
    radius=max(max(v[1] for v in vertices)-min(v[1] for v in vertices),max(v[2] for v in vertices)-min(v[2] for v in vertices))*.5
    mesh=bpy.data.meshes.new('Shared wheel preview')
    mesh.from_pydata([(v[0]*.43/radius,v[2]*.43/radius,v[1]*.43/radius) for v in vertices],[],[(ii[i],ii[i+2],ii[i+1]) for i in range(0,ni,3)])
    uv=mesh.uv_layers.new(name='UVMap')
    for loop in mesh.loops:uv.data[loop.index].uv=vertices[loop.vertex_index][6:8]
    mat=bpy.data.materials.new('Shared wheel atlas preview');mat.use_nodes=True
    tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(root/'assets/textures/vehicles/common/wheel.png'));tex.interpolation='Closest'
    shader=mat.node_tree.nodes.get('Principled BSDF');mat.node_tree.links.new(tex.outputs['Color'],shader.inputs['Base Color']);shader.inputs['Roughness'].default_value=1;shader.inputs['Specular IOR Level'].default_value=0
    mesh.materials.append(mat)
    for x in (-.94,.94):
        for y in (1.45,-1.35):
            obj=bpy.data.objects.new('Shared wheel (display only)',mesh);bpy.context.collection.objects.link(obj);obj.location=(x,y,.43);obj['exclude_from_body']=True
    image.pack()


def build_vehicle(detail=True):
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    b=VehicleBuilder()
    # Authored cross-sections form the haunches and hood as a single skin.
    stations=[(-2.40,.77,.60,.65),(-2.24,.97,.79,.82),(-1.91,1.065,.88,.88),
              (-1.35,1.085,1.005,.91),
              (-.80,1.015,.86,.82),(0,.99,.81,.79),
              (.72,1.02,.87,.81),(1.45,1.085,1.005,.85),
              (1.72,1.065,.965,.82),(2.02,1.00,.88,.79),(2.27,.91,.79,.80),(2.40,.78,.73,.80)]
    shell=b.loft('Continuous sculpted body',[(y,ring(w,h,t)) for y,w,h,t in stations],'BODY_TOP')
    for key in ('BODY_SIDE','BODY_SHADOW','BODY_FRONT','BODY_REAR','BLACK'):shell.data.materials.append(b.material(key))
    for y in (1.45,-1.35):
        for s in (-1,1):boolean_cut(shell,well_cutter(b,s,y))
    # Cabin void reaches below the sill. The windshield sees seats and floor.
    cut=b.box('Temporary cabin cavity',(-.705,-1.075,.36),(.705,.84,1.50),'BLACK');b.objects.remove(cut)
    boolean_cut(shell,cut)
    for f in shell.data.polygons:
        p=f.center;n=f.normal
        f.material_index=2 if n.z<-.1 else (1 if abs(n.x)>.45 else 0)
        if abs(p.x)<.71 and -1.08<p.y<.85 and p.z<.80:f.material_index=5
        f.use_smooth=n.z>.15 and p.z>.59
    shell_check=check_shell(shell)
    assert shell_check['wheel_intrusions']==0,shell_check
    bpy.context.scene['wheel_fit_report']=json.dumps(shell_check)
    print('WHEEL_FIT',json.dumps(shell_check))
    panes=canopy(b)
    if detail:
        # Real horseshoe opening, rim and deep inset. The nose shell is removed.
        outer=[(-.22,2.405,.19),(.22,2.405,.19),(.27,2.405,.26),(.275,2.405,.51),(.225,2.405,.675),(.13,2.405,.745),(0,2.405,.775),(-.13,2.405,.745),(-.225,2.405,.675),(-.275,2.405,.51),(-.27,2.405,.26)]
        cutter=b.add_mesh('Temporary grille cutter',[(x,y,z) for x,y,z in outer]+[(x,2.15,z) for x,y,z in outer],[tuple(range(len(outer))),tuple(reversed(range(len(outer),2*len(outer))))]+[(i,(i+1)%len(outer),len(outer)+(i+1)%len(outer),len(outer)+i) for i in range(len(outer))],'BLACK');b.objects.remove(cutter)
        boolean_cut(shell,cutter)
        recess(b,'Horseshoe grille',outer,(0,1,0),.19,'METAL')
        for s in (-1,1):
            headlamp(b,shell,s,.54,.665)
            headlamp(b,shell,s,.79,.695)
            # Intake is inset into the rear quarter, not a floating arch ribbon.
            points=[]
            for y,z in [(-.74,.46),(-.77,.70),(-.55,.70),(-.49,.46)]:
                hit,loc,_,_=shell.ray_cast(Vector((s*2,y,z)),Vector((-s,0,0)))
                assert hit,'Intake must follow the quarter skin'
                points.append(tuple(loc+Vector((s*.003,0,0))))
            cut=b.add_mesh('Temporary intake cutter',points+[tuple(Vector(p)-Vector((s*.07,0,0))) for p in points],[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],'BLACK');b.objects.remove(cut)
            boolean_cut(shell,cut)
            recess(b,'Quarter intake',points,(s,0,0),.035,'BODY_SIDE')
            pod=[(s*(.965+.110*math.cos(math.tau*i/6)),.81+.075*math.sin(math.tau*i/6),z) for z in (.993,1.028) for i in range(6)]
            pod.extend([(s*.965,.81,.974),(s*.965,.81,1.047)])
            mirror=b.add_mesh('Rounded mirror pod',pod,[(i,(i+1)%6,6+(i+1)%6,6+i) for i in range(6)]+[((i+1)%6,i,12) for i in range(6)]+[(6+i,6+(i+1)%6,13) for i in range(6)],'BODY_SIDE')
            for face in mirror.data.polygons:face.use_smooth=True
            b.box('Mirror stem',(min(s*.82,s*.92),.79,.887),(max(s*.82,s*.92),.83,.998),'LENS_DARK')
            b.box('Seat cushion',(s*.32-.21,-.43,.37),(s*.32+.21,.09,.48),'SEAM')
            b.add_mesh('Reclined bucket seat',[(s*.32+x,y,z) for x,y,z in [(-.20,-.46,.46),(.20,-.46,.46),(.20,-.32,.46),(-.20,-.32,.46),(-.16,-.60,.91),(.16,-.60,.91),(.16,-.47,.91),(-.16,-.47,.91)]],[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],'SEAM')
        b.box('Dashboard',(-.67,.57,.52),(.67,.80,.78),'BLACK')
        steering=Sheet()
        for i in range(8):
            a=math.tau*i/8;c=math.tau*(i+1)/8
            def point(t,r):return (.32+r*math.cos(t),.49-r*math.sin(t)*.55,.78+r*math.sin(t)*.835)
            steering.face([point(a,.135),point(c,.135),point(c,.109),point(a,.109)],'BLACK')
        rim=b.add_mesh('Left steering wheel',steering.vertices,steering.faces,'BLACK')
        b.box('Center tunnel',(-.12,-.57,.25),(.12,.65,.43),'LENS_DARK')
        for x in (-.15,.15):
            outer=[(x+.074*math.cos(math.tau*i/8),-2.42,.265+.074*math.sin(math.tau*i/8)) for i in range(8)]
            inner=[(px,py+.06,pz) for px,py,pz in inset(outer,.016)]
            sleeve=b.add_mesh('Recessed exhaust sleeve',outer+inner,[(i,(i+1)%8,8+(i+1)%8,8+i) for i in range(8)]+[tuple(range(8,16))],'METAL')
            sleeve.data.materials.append(b.material('BLACK'));sleeve.data.polygons[-1].material_index=1
        # Blank registration recesses. Runtime owns the lettering and plate state.
        for x,y,z,normal in [(.52,2.418,.33,(0,1,0)),(0,-2.418,.49,(0,-1,0))]:
            outer=[(x-.171,y,z-.092),(x+.171,y,z-.092),(x+.171,y,z+.092),(x-.171,y,z+.092)]
            recess(b,'Registration surround',outer,normal,.014,'LENS_DARK')
    # End receivers follow the curved shell in physical X/height coordinates.
    for f in shell.data.polygons:
        if f.center.y>1.90 and f.normal.y>.12:f.material_index=3
        elif f.center.y<-1.90 and f.normal.y<-.12:f.material_index=4
    # Round the joined front after its recesses are fitted so there are no
    # floating lamps or trim at the curved panel boundaries.
    for obj in b.objects:round_nose(obj)
    if detail:
        # Cut the landing after wrapping; a long fascia triangle must not
        # interpolate through the rigid registration rectangle.
        cut=b.box('Temporary front registration pocket',(.352,2.275,.241),(.688,2.50,.419),'BLACK')
        b.objects.remove(cut);boolean_cut(shell,cut)
    final_wheel_fit=check_shell(shell)
    assert final_wheel_fit['wheel_intrusions']==0,final_wheel_fit
    bpy.context.scene['wheel_fit_report']=json.dumps(final_wheel_fit)
    # The closed cabin floor covers these undersides. Spend those triangles
    # on the curved fascia and registration pocket instead.
    for obj in b.objects:
        if obj.name.startswith(('Seat cushion','Reclined bucket seat','Center tunnel')):
            bm=bmesh.new();bm.from_mesh(obj.data)
            bottom=min(v.co.z for v in bm.verts)
            faces=[f for f in bm.faces if all(abs(v.co.z-bottom)<1e-6 for v in f.verts)]
            bmesh.ops.delete(bm,geom=faces,context='FACES');bm.to_mesh(obj.data);bm.free()
    body=b.join();body.name='BODY';body.data.name='SpagattiShuBody'
    bm=bmesh.new();bm.from_mesh(body.data)
    bmesh.ops.dissolve_limit(bm,angle_limit=.00001,verts=list(bm.verts),edges=list(bm.edges),delimit={'MATERIAL','NORMAL'})
    bm.to_mesh(body.data);bm.free()
    assign_uvs(body)
    for obj in panes.values():assign_uvs(obj)
    for name,loc in {'WHEEL_FL':(.94,1.45,.43),'WHEEL_FR':(-.94,1.45,.43),'WHEEL_RL':(.94,-1.35,.43),'WHEEL_RR':(-.94,-1.35,.43)}.items():
        obj=bpy.data.objects.new(name,None);bpy.context.collection.objects.link(obj);obj.location=loc;obj.empty_display_type='SPHERE';obj.empty_display_size=.11
    body['plate_mounts_runtime_axes']=json.dumps(PLATE_MOUNTS)
    for end in ('front','rear'):
        x,y,z=PLATE_MOUNTS[end]['center']
        anchor=bpy.data.objects.new('PLATE_'+end.upper(),None);bpy.context.collection.objects.link(anchor);anchor.location=(x,z,y);anchor.empty_display_type='ARROWS';anchor.empty_display_size=.12
    return body,panes


def main():
    p=argparse.ArgumentParser();p.add_argument('--mesh',type=Path,required=True);p.add_argument('--blend',type=Path,required=True);p.add_argument('--blockout',action='store_true');p.add_argument('--texture',type=Path,default=Path(__file__).resolve().parents[1]/'assets/textures/vehicles/spagatti_shu/body.png')
    a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);body,panes=build_vehicle(not a.blockout)
    body.data.calc_loop_triangles()
    assert len(body.data.loop_triangles)<=SHAPE['triangle_budget'][1],('Opaque body exceeds the reference-pass triangle budget',len(body.data.loop_triangles))
    exports={'body':body,**panes};report={}
    for key,obj in exports.items():
        v,t=export_emesh(obj,a.mesh.parent/(key+'.emesh'),'spagatti_shu');report[key]={'vertices':v,'triangles':t}
    source_materials(body,panes,a.texture)
    bpy.ops.object.select_all(action='DESELECT');body.select_set(True);bpy.context.view_layer.objects.active=body
    a.blend.parent.mkdir(parents=True,exist_ok=True);bpy.ops.wm.save_as_mainfile(filepath=str(a.blend))
    report['wheel_fit']=json.loads(bpy.context.scene['wheel_fit_report'])
    (a.mesh.parent/'cook_report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
