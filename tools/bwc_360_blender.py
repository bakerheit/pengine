"""Original Belgian compact sedan with four connected opening doors.

Construction: X driver-left, Y nose, Z up. Runtime swaps Y/Z. The saved
Blender model mirrors X so -X is the driver's left, like the Grazer.
"""
import bpy,bmesh,math,json,sys
from pathlib import Path
from mathutils import Vector,Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from bwc_360_spec import ROOT,MODEL,TEXTURE,REGIONS,SHAPE,WHEELS,DRIVER,PLATE_MOUNTS
from vesper_vx91_blender import VehicleBuilder,export_emesh
from rodeo_grazer_cab import Sheet as BaseSheet
from bwc_360_fascia import soften
from bwc_360_doors import BODY_LEVELS, side_x, rear_edge, conform_quarter
class Sheet(BaseSheet):
    def finish(self,name,material,thickness=.03):
        obj=super().finish(name,material,thickness,edge_radius=.0015,edge_segments=1)
        bm=bmesh.new();bm.from_mesh(obj.data)
        bmesh.ops.dissolve_limit(bm,angle_limit=.001,verts=list(bm.verts),edges=list(bm.edges),delimit={'MATERIAL'})
        bm.to_mesh(obj.data);bm.free()
        return soften(obj)
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene;scene.name='BWC 360 — Belgian compact sedan'
b=VehicleBuilder();fixed=[];active=fixed;doors={};panes={};hinges={}
def mesh(name,vs,fs,key='PAINT'):
    obj=b.add_mesh(name,vs,fs,key);active.append(obj);return obj
def bevel(obj,width=.012,segments=2):
    bpy.context.view_layer.objects.active=obj
    m=obj.modifiers.new('Pressed edge radius','BEVEL');m.width=width;m.segments=segments
    m.limit_method='ANGLE';m.angle_limit=.65;bpy.ops.object.modifier_apply(modifier=m.name)
    return obj
def box(name,lo,hi,key='PAINT',radius=0):
    obj=b.box(name,lo,hi,key);active.append(obj)
    return bevel(obj,radius) if radius else obj
def beam(name,a,c,width,key='BLACK'):
    a=Vector(a);c=Vector(c);obj=box(name,(-width/2,-width/2,-(c-a).length/2),(width/2,width/2,(c-a).length/2),key,width*.15)
    obj.location=(a+c)/2;obj.rotation_euler=(c-a).to_track_quat('Z','Y').to_euler();return obj
def pane(name,points):
    obj=mesh(name,points,[tuple(range(len(points)))],'GLASS');active.remove(obj);panes[name]=obj
    desired=Vector((0,1,0) if name=='windshield' else ((0,-1,0) if name=='rear_glass' else (1 if sum(p[0] for p in points)>0 else -1,0,0)))
    bm=bmesh.new();bm.from_mesh(obj.data)
    for face in bm.faces:
        if face.normal.dot(desired)<0:face.normal_flip()
    bm.to_mesh(obj.data);bm.free()
def side(s,poly):return [(s*side_x(h),y,h) for y,h in poly]
def interp(stations,x):
    for (a,u),(c,v) in zip(stations,stations[1:]):
        if x<=c:return u+(v-u)*max(0,(x-a)/(c-a))
    return stations[-1][1]
def top_at(y,front):
    if front:return interp([(.86,.939),(1.04,.929),(1.30,.913),(1.65,.878),(1.94,.835),(2.19,.816)],y)
    return interp([(-2.23,.845),(-2.10,.872),(-1.70,.925),(-1.28,.942)],y)
def crown(name,sections):
    sheet=Sheet();rows=[]
    for y,h in sections:
        # Broad soft crown, with two subtle hood power creases.
        rows.append([(x,y,h+lift) for x,lift in [(-.65,0),(-.58,.010),(-.38,.022),(-.34,.026),(0,.033),(.34,.026),(.38,.022),(.58,.010),(.65,0)]])
    for a,c in zip(rows,rows[1:]):
        for i in range(len(a)-1):sheet.face([a[i],a[i+1],c[i+1],c[i]])
    obj=sheet.finish(name+' Cab stamping',b.material,.023);active.append(obj)
def fender(s,axle,lo,hi,front):
    stations=[(lo,.26)]+[(axle-.405*math.cos(i*math.pi/20),.325+.405*math.sin(i*math.pi/20)) for i in range(21)]+[(hi,.26)]
    vs=[]
    for y,h in stations:
        top=top_at(y,front)
        # The wheel crown grows out of a curved flank and tapers at each end.
        taper=.045*max(0,(abs(y)-1.85)/.38)
        flare=.016*math.exp(-((y-axle)/.43)**2)
        vs.extend([(s*x,y,z) for x,z in [(.65,h),(.811-taper,h),(.851+flare-taper,h+.013),(.871+flare-taper,h+.031),(.871-taper,top-.071),(.854-taper,top-.030),(.817-taper,top-.007),(.75,top+.008),(.65,top)]])
    fs=[];n=9
    for i in range(len(stations)-1):
        for j in range(n):fs.append((i*n+j,i*n+(j+1)%n,(i+1)*n+(j+1)%n,(i+1)*n+j))
    fs.extend([tuple(reversed(range(n))),tuple((len(stations)-1)*n+j for j in range(n))])
    obj=mesh(('Front' if front else 'Rear')+' integrated quarter',vs,fs,'SIDE')
    conform_quarter(obj,s,front)
for s in (-1,1):
    fender(s,1.27,.864,2.19,True);fender(s,-1.30,-2.23,-.894,False)

# Dark wheel tubs close the view through each wheel opening.
# Their inboard walls stay inside the full front steering envelope.
for s in (-1,1):
    for axle in (1.27,-1.30):
        arc=[(axle-.408*math.cos(i*math.pi/12),.325+.408*math.sin(i*math.pi/12)) for i in range(13)]
        outline=[(axle-.408,.24)]+arc+[(axle+.408,.24)]
        vs=[(s*.49,y,h) for y,h in outline]
        fs=[tuple(range(len(vs)))]
        for y,h in arc:vs.append((s*.66,y,h))
        for i in range(12):fs.append((i+1,i+2,16+i,15+i))
        # Short fore/aft returns close the corners below the arch spring line.
        for k,j in [(0,0),(14,12)]:
            vs.append((s*.66,outline[k][0],.24))
            fs.append((k,1 if k==0 else 13,15+j,len(vs)-1))
        obj=mesh('Matte inner wheel tub',vs,fs,'BLACK')
        bm=bmesh.new();bm.from_mesh(obj.data)
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        # Open-bottom tubs face the wheel cavity, including the inboard wall.
        for f in bm.faces:
            if len(f.verts)>4 and f.normal.x*s<0:f.normal_flip()
        bm.to_mesh(obj.data);bm.free()

crown('Hood',[(y,top_at(y,True)) for y in [.94,1.02,1.15,1.30,1.65,1.90,2.08,2.19]])
crown('Trunk',[(y,top_at(y,False)) for y in [-2.23,-2.17,-1.90,-1.70,-1.48,-1.28]])
box('Cab floor',(-.69,-.88,.25),(.69,.90,.28),'DASH')
box('Rear footwell floor',(-.625,-1.08,.25),(.625,-.88,.28),'DASH')
box('Front chassis',(-.49,.85,.27),(.49,2.16,.36),'BLACK')
box('Rear chassis',(-.49,-2.20,.27),(.49,-1.04,.36),'BLACK')

# A continuous arched canopy with a curved roof rail above each door.
roof_profile=[(-.84,1.321),(-.70,1.365),(-.50,1.388),(-.20,1.397),(.06,1.384),(.23,1.354),(.33,1.317)]
rail_inner=[(y*.93-.017,h-.032) for y,h in roof_profile]
cab=Sheet()
for s in (-1,1):
    # Door jambs use the same height stations as the outer skins.
    for (h,_),(z,_) in zip(BODY_LEVELS[1:],BODY_LEVELS[2:]):
        cab.face(side(s,[(.864,h),(.940,h),(.940,z),(.864,z)]))
    cab.face(side(s,[(.864,.90),(.940,.90),(.33,1.317),rail_inner[-1]]))
    # The B pillar is recessed behind a 6 mm moving shut line.
    heights=[h for h,x in BODY_LEVELS[1:]]+[1.08,1.24]
    cab.face([(s*(side_x(.259)-d),y,.259) for y,d in [(-.087,0),(-.063,0),(-.063,.038),(-.087,.038)]],'BLACK')
    for h,z in zip(heights,heights[1:]):
        cab.face([(s*(side_x(t)-.038),y,t) for y,t in [(-.087,h),(-.063,h),(-.063,z),(-.087,z)]],'BLACK')
    cab.face([(s*(side_x(t)-.038),y,t) for y,t in [(-.087,1.24),(-.063,1.24),(-.063,interp(rail_inner,-.063)),(-.087,interp(rail_inner,-.087))]],'BLACK')
    cab.face([(s*(side_x(interp(rail_inner,y))-d),y,interp(rail_inner,y)) for y,d in [(-.087,0),(-.063,0),(-.063,.038),(-.087,.038)]],'BLACK')
    cab.face(side(s,roof_profile+list(reversed(rail_inner))))
    cab.face(side(s,[(-1.28,.942),(-1.130,.90),rail_inner[0],roof_profile[0]]))
    cab.face(side(s,[(-.885,.230),(.94,.230),(.94,.259),(-.885,.259)]),'SIDE')
rows=[];weights=[(-1,0),(-.94,.45),(-.72,.9),(0,1),(.72,.9),(.94,.45),(1,0)]
for y,h in roof_profile:rows.append([(side_x(h)*q,y,h+.032*t) for q,t in weights])
for a,c in zip(rows,rows[1:]):
    for i in range(6):cab.face([a[i],a[i+1],c[i+1],c[i]])
# Curved cross sections on the front/rear glazing match the roof crown exactly.
def screen_ring(front):
    top=rows[-1] if front else rows[0]
    by,bh=(.94,.90) if front else (-1.28,.942)
    iy,ih,iw=(.923,.947,.786) if front else (-1.257,.976,.771)
    ty,th,tw=(.384,1.280,.699) if front else (-.884,1.285,.695)
    bottom=[(side_x(bh)*q,by,bh+.036*t) for q,t in weights]
    inner_bottom=[(iw*q,iy,ih+.016*t) for q,t in weights]
    inner_top=[(tw*q,ty,th+.030*t) for q,t in weights]
    outer=bottom+list(reversed(top));inner=inner_bottom+list(reversed(inner_top))
    cab.ring(outer,inner)
    return inner
wind=screen_ring(True);back=screen_ring(False)
# Cowl and rear shelf connect the broad panels with the same edge stations.
for front in (True,False):
    y,h=(.94,.90) if front else (-1.28,.942)
    outer=[(side_x(h)*q,y,h+.036*t) for q,t in weights]
    inner=[(.65*q,y,top_at(y,front)+.033*t) for q,t in weights]
    for i in range(6):cab.face([outer[i],outer[i+1],inner[i+1],inner[i]])
fixed.append(cab.finish('Cab continuous canopy',b.material,.025))
# Recessed weather seals back the moving roof shut lines, leaving the apertures open.
for s in (-1,1):
    vs=[]
    for y,h in rail_inner:
        for dz in [-.018,.004]:vs.append((s*(side_x(h+dz)-.038),y,h+dz))
    fs=[(i*2,i*2+1,i*2+3,i*2+2) for i in range(len(rail_inner)-1)]
    obj=mesh('Recessed upper door weather seal',vs,fs,'BLACK')
    bm=bmesh.new();bm.from_mesh(obj.data)
    for f in bm.faces:
        if f.normal.x*s<0:f.normal_flip()
    bm.to_mesh(obj.data);bm.free()

# Continue the recessed seals down the A/C pillars. A dark backdrop in
# a studio can conceal daylight here; these strips also cover oblique views.
for s in (-1,1):
    for label,points in [('A',[(.861,.89),rail_inner[-1]]),('C',[(-1.126,.89),rail_inner[0]])]:
        a,c=[Vector(p) for p in points];v=(c-a).normalized();n=Vector((-v.y,v.x))*.012
        vs=[]
        for p in (a-v*.012,c+v*.012):
            for q in (p-n,p+n):
                y,h=q;vs.append((s*(side_x(h)-.042),y,h))
        obj=mesh(label+' pillar recessed door seal',vs,[(0,1,3,2)],'BLACK')
        bm=bmesh.new();bm.from_mesh(obj.data)
        for f in bm.faces:
            if f.normal.x*s<0:f.normal_flip()
        bm.to_mesh(obj.data);bm.free()

pane('windshield',[(x,y+.005,h) for x,y,h in wind]);pane('rear_glass',[(x,y-.005,h) for x,y,h in back])
for x in [-.38,.35]:beam('Windshield wiper',(x-.20,.939,.968),(x+.18,.945,.960),.012)

for s in (1,-1):
    for row in ('front','rear'):
        name=('driver' if s>0 else 'passenger')+'_'+row
        doors[name]=[];active=doors[name];sheet=Sheet();rings=[]
        for h,x in BODY_LEVELS:
            if h<.265:continue
            lo=-.072 if row=='front' else rear_edge(h);hi=.858 if row=='front' else -.078
            rings.append([(s*x,y,h) for y in [lo,lo+.028,hi-.028,hi]])
        for a,c in zip(rings,rings[1:]):
            for i in range(3):sheet.face([a[i],a[i+1],c[i+1],c[i]],'SIDE')
        if row=='front':
            upper=[(y,interp(rail_inner,y)-.004) for y in [rail_inner[-1][0]-.002,rail_inner[-2][0],rail_inner[-3][0],-.072]]
            outer=[(-.072,.90),(.858,.90)]+upper
            inner=[(-.010,.950),(.753,.950),(.240,1.242),(.170,1.273),(.020,1.307),(-.010,1.309)]
            hy=.861
        else:
            upper=[(y,interp(rail_inner,y)-.004) for y in [-.078,rail_inner[3][0],rail_inner[2][0],rail_inner[1][0],rail_inner[0][0]+.002]]
            outer=[(-1.122,.90),(-.078,.90)]+upper
            inner=[(-1.025,.950),(-.143,.950),(-.143,1.310),(-.20,1.311),(-.47,1.302),(-.604,1.284),(-.741,1.252)]
            hy=-.075
        sheet.ring(side(s,outer),side(s,inner))
        active.append(sheet.finish(name+' door',b.material,.028))
        pane(name+'_glass',[(x-s*.008,y,h) for x,y,h in side(s,inner)])
        if row=='rear':beam('Rear quarter glass divider',(s*side_x(.951),-.835,.951),(s*side_x(1.270),-.678,1.270),.016)
        lo=-.015 if row=='front' else -.75;hi=.72 if row=='front' else -.15
        box('Door interior card',(min(s*.782,s*.806),lo,.36),(max(s*.782,s*.806),hi,.88),'DASH',.025)
        box('Door armrest',(min(s*.725,s*.78),lo+.10,.63),(max(s*.725,s*.78),hi-.08,.677),'BLACK',.013)
        handle_y=.085 if row=='front' else -.725
        box('Handle recess',(min(s*.848,s*.856),handle_y-.085,.818),(max(s*.848,s*.856),handle_y+.075,.849),'BLACK',.012)
        box('Body color pull handle',(min(s*.852,s*.881),handle_y-.079,.825),(max(s*.852,s*.881),handle_y+.079,.845),'PAINT',.009)
        # Thin molded impact strip across the center of each door.
        lo=-.066 if row=='front' else rear_edge(.53)+.009;hi=.850 if row=='front' else -.084
        box('Door protective molding',(min(s*.854,s*.866),lo,.521),(max(s*.854,s*.866),hi,.554),'BLACK',.008)
        if row=='front':
            beam('Mirror stalk',(s*.826,.737,.922),(s*.91,.754,.98),.028)
            box('Rounded mirror',(s*.961-.080,.677,.953),(s*.961+.080,.797,1.050),'PAINT',.039)
            mesh('Mirror lens',[(s*x,.672,z) for x,z in [(.903,.974),(1.020,.974),(1.020,1.030),(.903,1.030)]],[(0,1,2,3)],'METAL')
        hinges[name]=(s*.864,hy,.28)
active=fixed
# Depth follows the stamping; apertures are cut before the rounded returns.
from bwc_360_fascia import build_front
build_front(mesh,b.material,bevel,fixed)
from bwc_360_tail import build_tail,tail_y,transverse
build_tail(mesh,b.material,fixed)
for s in (-1,1):
    box('Front fender side indicator',(min(s*.858,s*.866),.919,.709),(max(s*.858,s*.866),1.031,.732),'AMBER',.010)
for name,mount in [(n,PLATE_MOUNTS[n]) for n in ('front',)]:
    _,h,y=mount['center'];s=mount['normal'][2];vs=[]
    for width,tall,d in [(.350,.192,.006),(.322,.170,.006),(.322,.170,-.004),(.350,.192,-.013)]:
        vs.extend([(x,y+s*d,h+z) for x,z in [(-width/2,-tall/2),(width/2,-tall/2),(width/2,tall/2),(-width/2,tall/2)]])
    fs=[]
    for a,c in [(0,4),(4,8),(12,0)]:fs.extend((a+i,a+(i+1)%4,c+(i+1)%4,c+i) for i in range(4))
    fs.extend([(8,9,10,11),(15,14,13,12)])
    bevel(mesh(name+' plate recess',vs,fs,'BLACK'),.002)

def badge(name,center,r,normal):
    # Real enamel disc and edge; UVs use the established BWC roundel cell.
    center=Vector(center);n=Vector(normal).normalized();u=Vector((1,0,0))
    if abs(n.x)>.9:u=Vector((0,1,0))
    if n.y<0:u=-u
    v=u.cross(n);vs=[tuple(center+u*(r*math.cos(i*math.tau/48))+v*(r*math.sin(i*math.tau/48))) for i in range(48)]
    obj=mesh(name,vs,[tuple(reversed(range(48)))],'BADGE');obj['badge_uv']=True
    if name=='BWC rear roundel':
        bm=bmesh.new();bm.from_mesh(obj.data)
        for face in bm.faces:
            if face.normal.dot(n)<0:face.normal_flip()
        bm.to_mesh(obj.data);bm.free()
    uv=obj.data.uv_layers.new(name='BWC atlas')
    for loop in obj.data.loops:
        i=loop.vertex_index;uv.data[loop.index].uv=((32+31*math.cos(i*math.tau/48))/256,1-(224-31*math.sin(i*math.tau/48))/256)
    bpy.context.view_layer.objects.active=obj
    mod=obj.modifiers.new('Enamel thickness','SOLIDIFY');mod.thickness=.003;mod.offset=-1;bpy.ops.object.modifier_apply(modifier=mod.name)
    return obj
badge('BWC front roundel',(0,2.025,top_at(2.025,True)+.035),.052,(0,.076,1))
badge('BWC rear roundel',(0,tail_y(.802)-.0012,.802),.052,(0,-1,.014/.245))
obj=mesh('360 model script',[(x,tail_y(z)-.001,z) for x,z in [(.265,.799),(.265,.849),(.415,.849),(.415,.799)]],[(0,1,2,3)],'LABEL')
transverse(obj);obj['label_uv']=True
uv=obj.data.uv_layers.new(name='BWC atlas');x0,y0,x1,y1=REGIONS['LABEL']
for loop in obj.data.loops:
    p=obj.data.vertices[loop.vertex_index].co
    uv.data[loop.index].uv=((x0+2+(1-(p.x-.265)/.150)*(x1-x0-4))/256,1-(y1-2-(p.z-.799)/.050*(y1-y0-4))/256)
# Five-seat interior and left-side controls.
box('Dashboard',(-.75,.65,.83),(.75,.89,.945),'DASH',.032)
box('Instrument hood',(.205,.58,.938),(.54,.79,1.002),'BLACK',.021)
mesh('Driver instruments',[(.219,.577,.944),(.526,.577,.944),(.526,.577,.987),(.219,.577,.987)],[(0,1,2,3)],'GAUGE')
for x in [-.38,.38]:
    box('Front seat cushion',(x-.20,-.06,.40),(x+.20,.37,.54),'CLOTH',.053)
    box('Front seat back',(x-.20,-.15,.49),(x+.20,-.042,1.026),'CLOTH',.045)
    box('Headrest',(x-.12,-.144,1.02),(x+.12,-.06,1.17),'CLOTH',.03)
box('Rear bench',(-.67,-.835,.40),(.67,-.44,.53),'CLOTH',.04)
box('Rear bench back',(-.65,-.925,.50),(.65,-.812,1.08),'CLOTH',.035)
for x in [-.43,.43]:box('Rear headrest',(x-.11,-.90,1.055),(x+.11,-.81,1.18),'CLOTH',.026)
box('Center console',(-.115,.05,.285),(.115,.61,.46),'DASH',.019)
beam('Gear lever',(0,.38,.44),(0,.34,.60),.024)
box('Shift knob',(-.031,.307,.57),(.031,.368,.628),'BLACK',.016)
bpy.ops.mesh.primitive_torus_add(major_segments=24,minor_segments=6,major_radius=.137,minor_radius=.014,location=(.38,.59,.94),rotation=(math.pi/2,0,0))
o=bpy.context.object;o.name='Left steering wheel';o.data.materials.append(b.material('BLACK'));active.append(o)
box('Steering spokes',(.267,.58,.920),(.493,.604,.957),'BLACK',.008)
box('Steering hub',(.335,.569,.907),(.425,.609,.981),'BLACK',.018)
badge('BWC steering roundel',(.38,.565,.944),.022,(0,-1,0))
for x,w in [(.48,.072),(.33,.045)]:box('Driver pedal',(x-w/2,.775,.307),(x+w/2,.815,.39),'BLACK',.004)
beam('Exhaust',(-.58,-2.17,.295),(-.58,-2.32,.295),.055,'METAL')

def uvmap(obj):
    if obj.get('badge_uv') or obj.get('label_uv'):return
    uv=obj.data.uv_layers.new(name='BWC atlas')
    for f in obj.data.polygons:
        key=obj.data.materials[f.material_index].name;x0,y0,x1,y1=REGIONS[key]
        axes=(0,1) if abs(f.normal.z)>.6 else ((1,2) if abs(f.normal.x)>.6 else (0,2))
        ranges=[(-1.06,1.06),(-2.33,2.25),(.235,1.44)]
        if key=='SIDE':axes=(1,2);ranges[2]=(.235,.94)
        points=[obj.data.vertices[obj.data.loops[i].vertex_index].co for i in f.loop_indices]
        if key in ['LAMP','LABEL','GAUGE']:ranges=[(min(p[a] for p in points),max(p[a] for p in points)) for a in range(3)]
        for li,p in zip(f.loop_indices,points):
            q=[max(0,min(1,(p[a]-ranges[a][0])/max(1e-6,ranges[a][1]-ranges[a][0]))) for a in axes]
            if key=='LABEL':q[0]=1-q[0]
            uv.data[li].uv=((x0+2+q[0]*(x1-x0-4))/256,1-(y1-2-q[1]*(y1-y0-4))/256)
def join(name,objects):
    bpy.context.view_layer.update()
    copies=[]
    for obj in objects:
        c=obj.copy();c.data=obj.data.copy();scene.collection.objects.link(c)
        c.data.transform(obj.matrix_world);c.matrix_world=Matrix.Identity(4)
        # One shared plan-view corner curve for panels, lamps, grilles and trim.
        # A common deformation keeps their relative depth and joins intact.
        for v in c.data.vertices:
            end=max(0,min(1,(abs(v.co.y)-1.70)/.50))
            v.co.y-=math.copysign(.135*end*(abs(v.co.x)/.88)**4,v.co.y)
        c.data.update();uvmap(c);copies.append(c)
    bpy.ops.object.select_all(action='DESELECT')
    for c in copies:c.select_set(True)
    bpy.context.view_layer.objects.active=copies[0]
    if len(copies)>1:bpy.ops.object.join()
    out=bpy.context.object;out.name=name
    bm=bmesh.new();bm.from_mesh(out.data);bmesh.ops.triangulate(bm,faces=list(bm.faces))
    bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.calc_area()<1e-9],context='FACES');bm.to_mesh(out.data);bm.free()
    return out

# 32-sided matte touring tires and sixteen slim radial alloy spokes.
active=[];vs=[];fs=[]
for x,r in [(-.092,.207),(-.099,.261),(-.075,.307),(-.045,.315),(.045,.315),(.075,.307),(.099,.261),(.092,.207)]:
    vs.extend([(x,r*math.cos(i*math.tau/32),r*math.sin(i*math.tau/32)) for i in range(32)])
for j in range(7):
    for i in range(32):fs.append((j*32+i,j*32+(i+1)%32,(j+1)*32+(i+1)%32,(j+1)*32+i))
mesh('Matte touring tire',vs,fs,'BLACK')
for i in range(32):
    a=i*math.tau/32
    for x in [-.036,.036]:
        o=box('Tread block',(x-.026,-.026,.312),(x+.026,.026,.316),'BLACK');o.rotation_euler.x=a
for s in (-1,1):
    vs=[];fs=[]
    for x,r in [(s*.098,.19),(s*.102,.212),(s*.09,.218)]:vs.extend([(x,r*math.cos(i*math.tau/32),r*math.sin(i*math.tau/32)) for i in range(32)])
    for j in range(2):
        for i in range(32):fs.append((j*32+i,j*32+(i+1)%32,(j+1)*32+(i+1)%32,(j+1)*32+i))
    mesh('Silver rim lip',vs,fs,'METAL')
    bpy.ops.mesh.primitive_cylinder_add(vertices=32,radius=.179,depth=.014,location=(s*.070,0,0),rotation=(0,math.pi/2,0))
    o=bpy.context.object;o.name='Brake rotor';o.data.materials.append(b.material('METAL'));active.append(o)
    for i in range(16):
        a=i*math.tau/16;vs=[]
        for r,w,x in [(.044,.010,s*.115),(.112,.014,s*.106),(.203,.008,s*.101)]:
            for dr,dw in [(0,-w),(0,w)]:
                yy=dw*math.cos(a)-r*math.sin(a);zz=dw*math.sin(a)+r*math.cos(a)
                vs.extend([(x,yy,zz),(x-s*.012,yy,zz)])
        fs=[]
        for j in range(2):
            k=j*4;fs.extend([(k,k+2,k+6,k+4),(k+1,k+5,k+7,k+3),(k,k+4,k+5,k+1),(k+2,k+3,k+7,k+6)])
        fs.extend([(0,1,3,2),(8,10,11,9)])
        mesh('Slender radial alloy spoke',vs,fs,'METAL')
    bpy.ops.mesh.primitive_cylinder_add(vertices=16,radius=.051,depth=.026,location=(s*.111,0,0),rotation=(0,math.pi/2,0))
    o=bpy.context.object;o.name='Wheel hub';o.data.materials.append(b.material('METAL'));active.append(o)
    badge('BWC wheel center',(s*.127,0,0),.027,(s,0,0))
wheel=join('front_wheel',active)
radius=max(math.hypot(v.co.y,v.co.z) for v in wheel.data.vertices)
for v in wheel.data.vertices:v.co*=.315/radius
exports={'body_open':join('Body',fixed)}
exports.update({name+'_door':join(name+' door',parts) for name,parts in doors.items()})
exports.update({name:join(name,[obj]) for name,obj in panes.items()})
exports['front_wheel']=wheel;exports['rear_wheel']=wheel.copy();exports['rear_wheel'].data=wheel.data.copy();scene.collection.objects.link(exports['rear_wheel'])
MODEL.mkdir(parents=True,exist_ok=True);report={}
for name,obj in exports.items():report[name]=export_emesh(obj,MODEL/(name+'.emesh'),'bwc_360')
# Separate closed body for static viewing. Wheels remain out of this mesh.
bpy.ops.object.select_all(action='DESELECT');copies=[]
for name,obj in exports.items():
    if 'wheel' in name:continue
    c=obj.copy();c.data=obj.data.copy();scene.collection.objects.link(c);c.select_set(True);copies.append(c)
bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();o=bpy.context.object
report['body']=export_emesh(o,MODEL/'body.emesh','bwc_360');bpy.data.objects.remove(o,do_unlink=True)
# Runtime variants retain the same UVs without baking glass into opaque paint.
for variant,names in {
    'body_drive':['body_open','driver_rear_door','passenger_rear_door'],
    'body_traffic':['body_open']+[n for n in exports if n.endswith('_door')],
}.items():
    bpy.ops.object.select_all(action='DESELECT');copies=[]
    for name in names:
        c=exports[name].copy();c.data=exports[name].data.copy();scene.collection.objects.link(c);c.select_set(True);copies.append(c)
    bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();o=bpy.context.object
    report[variant]=export_emesh(o,MODEL/(variant+'.emesh'),'bwc_360');bpy.data.objects.remove(o,do_unlink=True)
for obj in list(scene.objects):
    if obj not in exports.values():bpy.data.objects.remove(obj,do_unlink=True)
atlas=bpy.data.images.load(str(TEXTURE));atlas.pack();materials={}
for key in REGIONS:
    mat=bpy.data.materials.new('BWC '+key.lower());mat.use_nodes=True;p=mat.node_tree.nodes.get('Principled BSDF')
    tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=atlas;tex.interpolation='Closest';mat.node_tree.links.new(tex.outputs['Color'],p.inputs['Base Color'])
    p.inputs['Roughness'].default_value=.42
    if key in ['PAINT','SIDE']:p.inputs['Metallic'].default_value=.18
    if key in ['BLACK','DASH','CLOTH','GAUGE']:
        p.inputs['Roughness'].default_value=1;p.inputs['Specular IOR Level'].default_value=0
    if key=='METAL':p.inputs['Metallic'].default_value=.55;p.inputs['Roughness'].default_value=.32
    if key=='BADGE':p.inputs['Roughness'].default_value=.50;p.inputs['Specular IOR Level'].default_value=.18
    if key=='GLASS':
        p.inputs['Alpha'].default_value=.30;p.inputs['Roughness'].default_value=.14;mat.surface_render_method='DITHERED'
    materials[key]=mat
for name,obj in exports.items():
    keys=[obj.data.materials[f.material_index].name for f in obj.data.polygons]
    unique=list(dict.fromkeys(keys));obj.data.materials.clear()
    for k in unique:obj.data.materials.append(materials[k])
    for f,k in zip(obj.data.polygons,keys):f.material_index=unique.index(k)
    obj.data.transform(Matrix.Diagonal((-1,1,1,1)))
    bm=bmesh.new();bm.from_mesh(obj.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(obj.data);bm.free()
    if 'wheel' in name:
        obj.location=(-.755,1.27 if name=='front_wheel' else -1.30,.325)
        c=obj.copy();scene.collection.objects.link(c);c.location.x=.755
for name,point in hinges.items():
    s=1 if name.startswith('driver') else -1
    hinge=bpy.data.objects.new(name+' hinge',None);scene.collection.objects.link(hinge);hinge.location=(-point[0],point[1],point[2]);bpy.context.view_layer.update()
    for obj in [exports[name+'_door'],exports[name+'_glass']]:obj.parent=hinge;obj.matrix_parent_inverse=hinge.matrix_world.inverted()
    for frame,amount in [(1,0),(40,1),(80,1),(120,0)]:
        hinge.rotation_euler.z=-s*math.radians(62 if 'front' in name else 58)*amount;hinge.keyframe_insert(data_path='rotation_euler',frame=frame)
for name in ('front','rear'):
    p=PLATE_MOUNTS[name]['center'];o=bpy.data.objects.new(name+' plate anchor',None);scene.collection.objects.link(o)
    o.location=(-p[0],p[2],p[1]);o.empty_display_type='CUBE';o.empty_display_size=1;o.scale=(.305/2,.001,.152/2)
scene.frame_end=120;scene.frame_set(1);scene['Identity']='BWC / 360 — Belgium Working Coach'
scene['Axes']='Saved Blender -X left, +Y front, +Z up; exported +X left, +Y up, +Z front'
scene['Features']='Four opening doors; five seats; left hand drive; matte tires; established BWC roundels; blank plate mounts'
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            space=area.spaces.active;space.shading.type='MATERIAL';space.overlay.show_overlays=False
            space.region_3d.view_rotation=(Vector((-4,6,2.8))-Vector((0,0,.75))).to_track_quat('Z','Y')
            space.region_3d.view_location=(0,0,.75);space.region_3d.view_distance=5.5
bpy.ops.object.select_all(action='DESELECT')
bpy.ops.wm.save_as_mainfile(filepath=str(MODEL/'articulated.blend'))
gltf_args=dict(filepath=str(MODEL/'bwc_360.glb'),export_format='GLB',export_animations=True)
gltf_props=bpy.ops.export_scene.gltf.get_rna_type().properties
if 'export_animation_mode' in gltf_props and 'SCENE' in gltf_props['export_animation_mode'].enum_items.keys():
    gltf_args['export_animation_mode']='SCENE'
bpy.ops.export_scene.gltf(**gltf_args)
(MODEL/'cook_report.json').write_text(json.dumps(dict(meshes=report,shape=SHAPE,wheels=WHEELS,driver=DRIVER,hinges=hinges),indent=2)+'\n')
(MODEL/'plate_mounts.json').write_text(json.dumps(PLATE_MOUNTS,indent=2)+'\n')
print('BWC 360 COOK',json.dumps(report))
