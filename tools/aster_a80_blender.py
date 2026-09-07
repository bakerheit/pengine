"""Deterministic joined airframe; no offset glass/door skins or body wheels."""
import math
import sys
from pathlib import Path
import bpy
import bmesh
sys.path.insert(0,str(Path(__file__).resolve().parent))
from vesper_vx91_blender import VehicleBuilder, export_emesh
from aster_a80_spec import REGIONS, STATIONS, GEAR

ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/aster_a80'

def uv_rect(p,name):
    x0,y0,x1,y1=REGIONS[name]
    return ((x0+.5+p[0]*(x1-x0-1))/256,1-(y1-.5-p[1]*(y1-y0-1))/256)

def map_object(obj):
    uv=obj.data.uv_layers.new(name='UVMap')
    for poly in obj.data.polygons:
        name=obj.data.materials[poly.material_index].name
        points=[obj.data.vertices[obj.data.loops[i].vertex_index].co for i in poly.loop_indices]
        n=poly.normal
        axes=(0,1) if abs(n.z)>=max(abs(n.x),abs(n.y)) else ((1,2) if abs(n.x)>abs(n.y) else (0,2))
        bounds=[(min(v.co[a] for v in obj.data.vertices),max(v.co[a] for v in obj.data.vertices)) for a in axes]
        for loop,p in zip(poly.loop_indices,points):
            if obj.name=='Fuselage':
                # Continuous half-cylinder charts, shared by both sides.
                station=min(STATIONS,key=lambda s:abs(s[0]-p.y))
                angle=math.acos(max(-1,min(1,(p.z-station[2])/station[1])))
                ab=((p.y+16)/32,1-angle/math.pi)
            elif name=='FAN':
                ab=((p.x-obj['cx'])/1.5+.5,(p.z-obj['cz'])/1.5+.5)
            else:
                ab=tuple((p[a]-lo)/(hi-lo) if hi-lo>1e-6 else .5 for a,(lo,hi) in zip(axes,bounds))
            uv.data[loop].uv=uv_rect(ab,name)

def finish(b,name):
    for obj in b.objects:
        bm=bmesh.new();bm.from_mesh(obj.data)
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        bm.to_mesh(obj.data);bm.free();obj.data.update()
        map_object(obj)
    mesh=b.join();mesh.name=name;mesh.data.name=name
    return mesh

def wing(b,s,tail=False):
    sections=[(1.0,-10,-14,4.60),(3.2,-11.0,-14.6,4.90),(6.2,-13,-15.1,5.35)] if tail else [
        (1.2,4.5,-4,2.8),(4.8,3.5,-4.5,2.8),(10,-.5,-6,3.1),(14,-4,-6.5,3.5)]
    verts=[]
    for x,lead,trail,h in sections:
        verts.extend([(s*x,lead,h),(s*x,lead-(lead-trail)*.25,h+.20),
                      (s*x,trail,h+.015),(s*x,lead-(lead-trail)*.25,h-.13)])
    faces=[(3,2,1,0)]
    for a in range(0,len(verts)-4,4):
        for j in range(4):faces.append((a+j,a+(j+1)%4,a+4+(j+1)%4,a+4+j))
    faces.append(tuple(range(len(verts)-4,len(verts))))
    b.add_mesh(('Tailplane' if tail else 'Wing')+str(s),verts,faces,'WING')

def engine(b,s):
    x=s*4.4;h=1.92;N=12
    # One closed shell runs outside, around the lip, into the recessed fan.
    rings=[(-.5,.48),(-.2,.74),(.7,.91),(2.8,.97),(3.5,.90),
           (3.62,.80),(3.54,.73),(3.02,.71)]
    verts=[(x+math.sin(i*2*math.pi/N)*r,y,h+math.cos(i*2*math.pi/N)*r) for y,r in rings for i in range(N)]
    faces=[]
    for k in range(len(rings)-1):
        for i in range(N):faces.append((k*N+i,k*N+(i+1)%N,(k+1)*N+(i+1)%N,(k+1)*N+i))
    faces.extend([tuple(reversed(range(N))),tuple(range((len(rings)-1)*N,len(rings)*N))])
    obj=b.add_mesh('Nacelle'+str(s),verts,faces,'ENGINE')
    for mat in ('METAL','SHADOW','FAN'):obj.data.materials.append(b.material(mat))
    obj['cx']=x;obj['cz']=h
    for p in obj.data.polygons:
        if p.index in range(4*N,6*N):p.material_index=1
        if p.index>=6*N:p.material_index=2
    obj.data.polygons[-1].material_index=3
    b.loft('Pylon'+str(s),[(.3,[(x-.13,2.5),(x-.13,3.0),(x+.13,3.0),(x+.13,2.5)]),
                          (2.8,[(x-.13,2.5),(x-.13,3.0),(x+.13,3.0),(x+.13,2.5)])],'WHITE')

def build():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    b=VehicleBuilder()
    b.loft('Fuselage',[(y,[(math.sin(i*math.pi/8)*r,c+math.cos(i*math.pi/8)*r) for i in range(16)]) for y,r,c in STATIONS],'FUSELAGE')
    for s in (-1,1):wing(b,s);wing(b,s,True);engine(b,s)
    profile=[(-14.5,4.5),(-9,4.6),(-12.2,9.2),(-14.6,9.2)]
    verts=[(x,y,z) for x in (-.12,.12) for y,z in profile]
    b.add_mesh('VerticalFin',verts,[(3,2,1,0),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],'TAIL')
    for x,y,low,high in ((0,11.1,.42,2.45),(-2.3,-1.5,.58,2.75),(2.3,-1.5,.58,2.75)):
        b.box('GearStrut'+str(x),(x-.105,y-.105,low),(x+.105,y+.105,high),'METAL')
        # Narrow hinged-looking doors are real thick panels, not body overlays.
        if x:
            a,c=sorted((x+math.copysign(.13,x),x+math.copysign(.20,x)))
            b.box('GearDoor'+str(x),(a,y-.40,1.35),(c,y+.40,2.55),'WHITE')
    body=finish(b,'AsterA80Body')
    export_emesh(body,MODEL/'body.emesh','aster_a80')
    # Separate six-wheel landing gear cook. Not a four-wheel road-car rig.
    w=VehicleBuilder()
    w.materials=b.materials
    for j,(cx,ch,cy,r,width) in enumerate(GEAR):
        N=12
        rings=[(-width/2,r*.84),(-width*.34,r),(width*.34,r),(width/2,r*.84)]
        verts=[(cx+dx,cy+math.sin(i*math.pi/6)*rad,ch+math.cos(i*math.pi/6)*rad) for dx,rad in rings for i in range(N)]
        faces=[tuple(reversed(range(N)))]
        for k in range(3):
            for i in range(N):faces.append((k*N+i,k*N+(i+1)%N,(k+1)*N+(i+1)%N,(k+1)*N+i))
        faces.append(tuple(range(3*N,4*N)))
        obj=w.add_mesh('Wheel'+str(j),verts,faces,'RUBBER')
        obj.data.materials.append(w.material('SHADOW'))
        for face in obj.data.polygons:
            if face.index not in (0,len(obj.data.polygons)-1):face.material_index=1
    gear=finish(w,'AsterA80LandingWheels')
    export_emesh(gear,MODEL/'gear.emesh','aster_a80')
    # Combined inspection cook only; runtime retains body/gear separation.
    bpy.ops.object.select_all(action='DESELECT');body.select_set(True);gear.select_set(True)
    bpy.context.view_layer.objects.active=body
    atlas=bpy.data.images.load(str(ROOT/'assets/textures/vehicles/aster_a80/body.png'),check_existing=True)
    atlas.pack()
    for mat in set(body.data.materials[:]+gear.data.materials[:]):
        mat.use_nodes=True
        bsdf=mat.node_tree.nodes.get('Principled BSDF')
        tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=atlas;tex.interpolation='Closest'
        mat.node_tree.links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
        bsdf.inputs['Roughness'].default_value=1.0
    bpy.ops.wm.save_as_mainfile(filepath=str(MODEL/'source.blend'))
    bpy.ops.object.join();export_emesh(body,MODEL/'preview.emesh','aster_a80')

if __name__=='__main__':build()
