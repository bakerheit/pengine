"""Joined open-cockpit speedboat, final topology before semantic UVs."""
import math
import sys
from pathlib import Path
import bpy
import bmesh
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from vesper_vx91_blender import VehicleBuilder,export_emesh
from marlin_sprint_spec import REGIONS,STATIONS
ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/marlin_sprint'

def flip_faces(obj,predicate):
    bm=bmesh.new();bm.from_mesh(obj.data)
    bmesh.ops.reverse_faces(bm,faces=[f for f in bm.faces if predicate(f.normal)])
    bm.to_mesh(obj.data);bm.free();obj.data.update()

class BoatBuilder(VehicleBuilder):
    def panel(self,name,vertices,material,desired):
        obj=super().panel(name,vertices,material,desired)
        # Recalculation has no reliable outside for a lone open surface.
        flip_faces(obj,lambda n:n.dot(Vector(desired))<0)
        return obj

def beam(b,name,a,c,r,material,sides=6):
    a,c=Vector(a),Vector(c);axis=(c-a).normalized()
    ref=Vector((0,0,1)) if abs(axis.z)<.9 else Vector((1,0,0))
    u=axis.cross(ref).normalized()*r;v=axis.cross(u).normalized()*r
    vertices=[tuple(p+u*math.cos(i*2*math.pi/sides)+v*math.sin(i*2*math.pi/sides)) for p in (a,c) for i in range(sides)]
    faces=[tuple(reversed(range(sides))),tuple(range(sides,2*sides))]
    faces.extend((i,(i+1)%sides,(i+1)%sides+sides,i+sides) for i in range(sides))
    return b.add_mesh(name,vertices,faces,material)

def map_uv(obj):
    uv=obj.data.uv_layers.new(name='UVMap')
    for poly in obj.data.polygons:
        material=obj.data.materials[poly.material_index].name
        x0,y0,x1,y1=REGIONS[material]
        n=poly.normal
        axes=(0,1) if abs(n.z)>=max(abs(n.x),abs(n.y)) else ((0,2) if abs(n.y)>abs(n.x) else (1,2))
        bounds=[(min(v.co[a] for v in obj.data.vertices),max(v.co[a] for v in obj.data.vertices)) for a in axes]
        for i in poly.loop_indices:
            p=obj.data.vertices[obj.data.loops[i].vertex_index].co
            if material=='HULL':ab=((p.y+3.10)/6.60,(p.z+.48)/1.31)
            elif material=='DECK':ab=(p.x/2.36+.5,(p.y+3.10)/6.60)
            else:ab=tuple((p[a]-lo)/(hi-lo) if hi-lo>1e-6 else .5 for a,(lo,hi) in zip(axes,bounds))
            u,v=(max(0,min(1,t)) for t in ab)
            uv.data[i].uv=((x0+1+u*(x1-x0-2))/256,1-(y1-1-v*(y1-y0-2))/256)

def build():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    b=BoatBuilder()
    # Connected seven-point open U cross-sections, not a capped solid box.
    verts=[]
    for y,w,h in STATIONS:
        rise=max(0,(y-.55)/2.95)
        keel=-.48+rise*.97
        chine=-.14+rise*.66
        verts.extend([(x,y,z) for x,z in [(-w,h),(-w*.90,chine),(-w*.52,keel+.14),
                     (0,keel),(w*.52,keel+.14),(w*.90,chine),(w,h)]])
    faces=[tuple(reversed(range(7)))]
    for j in range(len(STATIONS)-1):
        faces.extend((j*7+i,j*7+i+1,(j+1)*7+i+1,(j+1)*7+i) for i in range(6))
    faces.append(tuple(range((len(STATIONS)-1)*7,len(STATIONS)*7)))
    hull=b.add_mesh('ContinuousHull',verts,faces,'HULL')
    # Open-top hull is intentionally not a closed solid. Its first port face
    # must point outboard; recalc otherwise chooses the empty cockpit side.
    if hull.data.polygons[1].normal.x>0:flip_faces(hull,lambda n:True)
    # Raised foredeck, real cockpit opening, and narrow side coamings.
    for j in range(4,len(STATIONS)-1):
        y,w,h=STATIONS[j];yy,ww,hh=STATIONS[j+1]
        for s in (-1,1):
            b.panel('Foredeck',[(0,y,h+.065),(s*w,y,h),(s*ww,yy,hh),(0,yy,hh+.065)],'DECK',(0,0,1))
    for s in (-1,1):
        for j in range(4):
            y,w,h=STATIONS[j];yy,ww,hh=STATIONS[j+1]
            b.panel('Coaming',[(s*w,y,h),(s*(w-.18),y,h-.025),
                (s*(ww-.18),yy,hh-.025),(s*ww,yy,hh)],'DECK',(0,0,1))
            b.panel('CockpitLiner',[(s*(w-.18),y,h-.025),(s*.76,y,.13),
                (s*.76,yy,.13),(s*(ww-.18),yy,hh-.025)],'CREAM',(-s,0,0))
        # Dark rub rail follows the sheer instead of a floating overlay skin.
        for j in range(len(STATIONS)-1):
            y,w,h=STATIONS[j];yy,ww,hh=STATIONS[j+1]
            beam(b,'RubRail',(s*w,y,h-.04),(s*ww,yy,hh-.04),.026,'DARK',4)
    b.box('CockpitFloor',(-.76,-2.65,.09),(.76,.55,.13),'FLOOR')
    b.box('SternMotorDeck',(-.77,-3.08,.35),(.77,-2.65,.61),'DECK')
    b.box('FrontBulkhead',(-.89,.40,.13),(.89,.55,.77),'CREAM')
    # Chunky upholstered seats are shaped lofts, not cube seats.
    for s in (-1,1):
        x=s*.51
        b.loft('BucketBase',[(-1.15,[(x-.31,.25),(x-.35,.45),(x+.35,.45),(x+.31,.25)]),
                             (-.40,[(x-.28,.26),(x-.32,.45),(x+.32,.45),(x+.28,.26)])],'VINYL')
        b.loft('BucketBack',[(-1.22,[(x-.28,.36),(x-.31,.91),(x+.31,.91),(x+.28,.36)]),
                             (-1.05,[(x-.28,.36),(x-.28,.86),(x+.28,.86),(x+.28,.36)])],'VINYL')
    b.box('RearBenchBase',(-.78,-2.59,.22),(.78,-1.99,.44),'VINYL')
    b.loft('RearBenchBack',[(-2.62,[(-.78,.38),(-.72,.78),(.72,.78),(.78,.38)]),
                           (-2.45,[(-.78,.38),(-.70,.74),(.70,.74),(.78,.38)])],'VINYL')
    b.box('DashBase',(-.86,.04,.63),(.86,.42,.80),'DARK')
    b.panel('DashInstruments',[(.16,.035,.66),(.80,.035,.66),(.80,.16,.82),(.16,.16,.82)],'DASH',(0,-1,1))
    # Thick glass panes own both sides. No coincident windshield cards.
    for s in (-1,1):
        pts=[(s*.02,.57,.83),(s*1.00,.57,.83),(s*.88,.05,1.43),(s*.02,.05,1.43)]
        back=[(x,y+.018,z) for x,y,z in pts]
        b.add_mesh('SplitWindscreen',pts+back,[(0,1,2,3),(7,6,5,4),
            (0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],'GLASS')
        for a,c in ((pts[0],pts[1]),(pts[1],pts[2]),(pts[2],pts[3])):
            beam(b,'ScreenFrame',a,c,.022,'METAL',4)
        pts=[(s*1.00,.57,.83),(s*1.03,-.51,.78),(s*.88,.05,1.43)]
        b.panel('WindshieldWing',pts,'GLASS',(s,0,1))
        # Solidify side panes for visibility from the seat and outside.
        side=b.objects[-1];mod=side.modifiers.new('GlassThickness','SOLIDIFY');mod.thickness=.018
        bpy.context.view_layer.objects.active=side;bpy.ops.object.modifier_apply(modifier=mod.name)
        beam(b,'WingFrame',pts[1],pts[2],.022,'METAL',4)
    beam(b,'WindshieldDivider',(0,.58,.83),(0,.05,1.43),.024,'METAL',4)
    # Visible helm wheel is boat equipment, not a road-wheel rig.
    cx,cy,cz=.51,-.18,.75
    verts=[(cx+( .16+.021*math.cos(k*math.pi/2))*math.cos(j*math.pi/5),
            cy+.021*math.sin(k*math.pi/2),cz+(.16+.021*math.cos(k*math.pi/2))*math.sin(j*math.pi/5))
           for j in range(10) for k in range(4)]
    faces=[(j*4+k,j*4+(k+1)%4,((j+1)%10)*4+(k+1)%4,((j+1)%10)*4+k) for j in range(10) for k in range(4)]
    b.add_mesh('HelmWheel',verts,faces,'DARK')
    for j in range(3):
        a=j*2*math.pi/3
        beam(b,'HelmSpoke',(cx,cy,cz),(cx+.145*math.cos(a),cy,cz+.145*math.sin(a)),.016,'METAL',4)
    b.box('SwimStep',(-.81,-3.65,.025),(.81,-3.12,.12),'TEAK')
    for s in (-1,1):
        beam(b,'SwimBrace',(s*.62,-3.08,-.16),(s*.62,-3.55,.015),.035,'METAL',4)
        b.box('SternVent',(s*.66-.12,-3.125,.30),(s*.66+.12,-3.105,.45),'DARK')
        for y in (-2.7,2.0):
            w=.85 if y<0 else .64
            beam(b,'CleatStem',(s*w,y,.76),(s*w,y,.84),.027,'METAL',4)
            beam(b,'CleatBar',(s*w,y-.10,.84),(s*w,y+.10,.84),.025,'METAL',4)
        b.box('NavLamp',(s*1.00-.05,.72,.83),(s*1.00+.05,.84,.90),'RED' if s<0 else 'GREEN')
    for obj in b.objects:map_uv(obj)
    body=b.join();body.name='MarlinSprint22';body.data.name=body.name
    export_emesh(body,MODEL/'body.emesh','marlin_sprint')
    atlas=bpy.data.images.load(str(ROOT/'assets/textures/vehicles/marlin_sprint/body.png'),check_existing=True);atlas.pack()
    for mat in body.data.materials:
        mat.use_nodes=True
        tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=atlas;tex.interpolation='Closest'
        bsdf=mat.node_tree.nodes.get('Principled BSDF');bsdf.inputs['Roughness'].default_value=.9
        mat.node_tree.links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
    bpy.ops.wm.save_as_mainfile(filepath=str(MODEL/'source.blend'))

if __name__=='__main__':build()
