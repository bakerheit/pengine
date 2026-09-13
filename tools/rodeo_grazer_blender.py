"""Original XtraCab-inspired truck. Intermediate axes X-left/Y-front/Z-up;
cooked axes X-left/Y-up/Z-front; saved Blender axes -X-left/Y-front/Z-up.
Body, both doors, glazing and four animated wheels remain separate.
"""
import bpy,bmesh,math,json,sys
from pathlib import Path
from mathutils import Vector,Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from rodeo_grazer_spec import MODEL,TEXTURE,REGIONS,COLORS,WHEELS,SHAPE,DOOR,DRIVER,ROUNDING,PLATE_MOUNTS,EMBLEMS,EMBLEM_DEPTH,EMBLEM_BEVEL
from vesper_vx91_blender import VehicleBuilder,export_emesh
from rodeo_grazer_cab import cab as continuous_cab,door as continuous_door
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene;scene.name='Rodeo Grazer 4x4'
b=VehicleBuilder();fixed=[];doors={1:[],-1:[]};panes={};active=fixed
def mesh(name,vs,fs,key='PAINT'):
    o=b.add_mesh(name,vs,fs,key);active.append(o);return o
def rounded_edges(o,label,width,segments=3,angle=math.radians(35)):
    if width<=0:return o
    bpy.context.view_layer.objects.active=o
    m=o.modifiers.new(label,'BEVEL');m.width=width;m.segments=segments
    m.limit_method='ANGLE';m.angle_limit=angle
    bpy.ops.object.modifier_apply(modifier=m.name)
    return o
def box(name,lo,hi,key='PAINT',bevel=0,segments=2):
    o=b.box(name,lo,hi,key);active.append(o)
    return rounded_edges(o,'Rolled edges',bevel,segments)
def panel(name,vs,key='PAINT',normal=None):
    o=mesh(name,vs,[tuple(range(len(vs)))],key)
    if normal is None:
        center=sum((Vector(v) for v in vs),Vector())/len(vs)
        if key=='GLASS':normal=(math.copysign(1,center.x),0,0) if abs(center.x)>.5 else (0,math.copysign(1,center.y),0)
        elif key in ['BADGE','GAUGE']:normal=(0,math.copysign(1,center.y) if key=='BADGE' else -1,0)
    if normal is not None:
        bm=bmesh.new();bm.from_mesh(o.data)
        for f in bm.faces:
            if f.normal.dot(Vector(normal))<0:f.normal_flip()
        bm.to_mesh(o.data);bm.free()
    return o
def beam(name,a,c,width,key='BLACK',bevel=0):
    a=Vector(a);c=Vector(c);mid=(a+c)*.5
    o=box(name,(-width/2,-width/2,-(c-a).length/2),(width/2,width/2,(c-a).length/2),key,bevel,3)
    o.rotation_euler=(c-a).to_track_quat('Z','Y').to_euler();o.location=mid
    return o
def rodeo_emblem(name):
    """Cast the approved R with a real open counter and thin metal sidewalls.

    Author in face-on coordinates, then mirror X only for the rear mounting so
    the R reads correctly from outside both ends of the truck. No decal card.
    """
    mount=EMBLEMS[name];cx,cy,cz=mount['center'];side=mount['normal']
    # Polygonal tracing of the selected concept, not a font glyph. Opposite
    # winding on the inner contour makes the bowl a hole in the cast metal.
    outline=[(0,0),(.22,0),(.22,.44),(.79,.44),(.90,.55),(.90,.88),
             (.80,1),(.06,1),(0,.94)]
    counter=[(.22,.61),(.22,.81),(.65,.81),(.71,.76),(.71,.66),(.65,.61)]
    leg=[(.47,.44),(.72,.44),(1.18,0),(.89,0)]
    for label,contours,key in [('bowl',[outline,counter],'BADGE_DARK'),
                               ('leg',[leg],'BADGE_RUST')]:
        curve=bpy.data.curves.new('Rodeo '+name+' '+label,'CURVE')
        curve.dimensions='2D';curve.resolution_u=1;curve.fill_mode='BOTH'
        curve.extrude=EMBLEM_DEPTH/2;curve.bevel_depth=EMBLEM_BEVEL
        curve.bevel_resolution=0
        for contour in contours:
            spline=curve.splines.new('POLY');spline.points.add(len(contour)-1)
            for point,(x,y) in zip(spline.points,contour):
                point.co=(x/1.18*mount['width'],y*mount['height'],0,1)
            spline.use_cyclic_u=True
        obj=bpy.data.objects.new('Rodeo '+name+' '+label,curve)
        scene.collection.objects.link(obj)
        bpy.ops.object.select_all(action='DESELECT');obj.select_set(True)
        bpy.context.view_layer.objects.active=obj;bpy.ops.object.convert(target='MESH')
        obj=bpy.context.object
        obj.data.materials.append(b.material(key));obj.data.materials.append(b.material('BADGE_METAL'))
        for face in obj.data.polygons:
            face.material_index=0 if abs(face.normal.z)>.99 else 1
        for vertex in obj.data.vertices:
            x,y,z=vertex.co
            vertex.co=(cx+side*(x-mount['width']/2),
                       cz+side*(z+EMBLEM_DEPTH/2),cy+y-mount['height']/2)
        # The axis swap above reverses winding on both mounts.
        bm=bmesh.new();bm.from_mesh(obj.data);bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
        bm.to_mesh(obj.data);bm.free();fixed.append(obj)
def wall(name,s,poly,key='SIDE',inner=.82):
    n=len(poly);vs=[(s*x,y,z) for x in [inner,.90] for y,z in poly]
    return mesh(name,vs,[tuple(reversed(range(n))),tuple(range(n,n*2))]+
                [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)],key)
def crown(name,sections,key='PAINT'):
    # Low-poly pressed sheet: broad center, rolled shoulders, narrow edge.
    vs=[]
    for y,w,h,drop in sections:
        vs.extend([(w*x,y,h-drop*d) for x,d in [(-1,1),(-.94,.38),(-.72,.06),(0,0),(.72,.06),(.94,.38),(1,1)]])
    fs=[(i*7+j,i*7+j+1,(i+1)*7+j+1,(i+1)*7+j) for i in range(len(sections)-1) for j in range(6)]
    o=mesh(name,vs,fs,key)
    bpy.context.view_layer.objects.active=o
    mod=o.modifiers.new('Stamped sheet thickness','SOLIDIFY');mod.thickness=.035
    bpy.ops.object.modifier_apply(modifier=mod.name)
    # The authored profile supplies curvature; preserve the common fender edge.
    return o
def fender(s,axle,lo,hi,top):
    arc_steps=ROUNDING['fender_arc_steps']
    stations=([(lo,.43)] if lo<axle-.49-1e-5 else [])+[(axle-.49*math.cos(i*math.pi/arc_steps),.405+.49*math.sin(i*math.pi/arc_steps)) for i in range(arc_steps+1)]+[(hi,.43)]
    vs=[]
    for z,bottom in stations:
        vs.extend([(s*x,z,h) for x,h in [(.65,bottom),(.83,bottom+.006),(.895,bottom+.020),(.935,bottom+.060),(.948,top-.095),(.943,top-.050),(.928,top-.018),(.90,top-.004),(.65,top)]])
    fs=[]
    for i in range(len(stations)-1):
        for j in range(9):fs.append((i*9+j,i*9+(j+1)%9,(i+1)*9+(j+1)%9,(i+1)*9+j))
    fs += [tuple(reversed(range(9))),tuple((len(stations)-1)*9+j for j in range(9))]
    o=mesh('Flared fender',vs,fs,'SIDE')
    # The extra stations and shoulder bands make a smooth low-poly fender without
    # beveling every internal grid edge into unnecessary hidden triangles.
    # Keep the broad outer flanks flat; only the crown receives restrained smoothing.
    for f in o.data.polygons:f.use_smooth=(f.normal.z>.24 and abs(f.normal.x)<.78)
    # Inner dark arches stop short of the swept tire volume.
    for i in range(arc_steps):
        a=math.pi*i/arc_steps;c=math.pi*(i+1)/arc_steps
        panel('Rolled arch lip',[(s*x,axle-.49*math.cos(t),.405+.49*math.sin(t))
                               for x,t in [( .90,a),(.948,a),(.948,c),(.90,c)]],'PAINT')

# Chassis, open wheel pockets and raised front sheet metal.
for s in [-1,1]:
    box('Chassis rail',(s*.45-.045,-2.43,.31),(s*.45+.045,2.15,.42),'BLACK')
    fender(s,1.43,.94,2.32,1.065)
    fender(s,-1.50,-2.52,-.76,1.09)
    box('Rocker',(s*.87-.035,-.70,.39),(s*.87+.035,.94,.46),'PAINT',ROUNDING['trim'],3)
    box('Bed cap',(s*.89-.055,-2.52,1.085),(s*.89+.055,-.76,1.105),'CHROME',ROUNDING['trim'],3)
    box('Mud flap',(s*.82-.13,-2.03,.23),(s*.82+.13,-1.99,.54),'BLACK')
crown('Pressed hood',[(y,.650,h,h-1.065) for y,h in
                      [(.94,1.065),(.99,1.075),(1.08,1.082),(1.88,1.075),
                       (2.18,1.071),(2.28,1.056),(2.32,1.042)]])
box('Hood leading edge',(-.65,2.30,1.001),(.65,2.325,1.042),'PAINT',ROUNDING['trim'],3)
box('Cab floor',(-.84,-.70,.40),(.84,.94,.43),'DASH')
box('Firewall',(-.83,.88,.43),(.83,.94,1.065),'DASH')
cab_shell,cab_glass=continuous_cab(b.material);fixed.append(cab_shell)
for name,points in cab_glass.items():
    glass=panel(name,points,'GLASS');fixed.remove(glass);panes[name]=glass
for x in [-.42,.40]:beam('Windshield wiper',(x-.23,.88,1.11),(x+.22,.90,1.12),.014)

# Two complete opening front doors. Small rear XtraCab panes remain fixed.
for s in [-1,1]:
    active=doors[s];name='driver' if s>0 else 'passenger'
    door_shell,door_glass=continuous_door(s,b.material);active.append(door_shell)
    glass=panel(name+' glass',door_glass,'GLASS');active.remove(glass);panes[name+'_glass']=glass
    card=wall(name+' inner card',s,[(-.23,.47),(.85,.47),(.85,1.04),(-.23,1.04)],'DASH',inner=.848)
    for v in card.data.vertices:
        if abs(v.co.x)>.86:v.co.x=s*.853
    box(name+' handle',(s*.913-.01,-.245,.99),(s*.913+.01,-.085,1.035),'CHROME',.014)
    box(name+' armrest',(s*.82-.035,.03,.78),(s*.82+.035,.48,.825),'BLACK',.018)
    beam(name+' mirror stalk',(s*.89,.73,1.06),(s*1.03,.73,1.13),.035)
    box(name+' mirror',(s*1.065-.10,.65,1.11),(s*1.065+.10,.76,1.275),'BLACK',.03)
    panel(name+' mirror lens',[(s*x,.644,z) for x,z in [( .988,1.14),(1.14,1.14),(1.14,1.25),(.988,1.25)]],'CHROME')
active=fixed
# Deep open bed: liner, pressed ribs, tubs, tie-down eyes, and tailgate.
box('Bed floor',(-.64,-2.49,.62),(.64,-.77,.65),'BED')
for lo,hi in [(-2.49,-1.99),(-1.01,-.77)]:box('Bed floor ends',(-.84,lo,.62),(.84,hi,.65),'BED')
box('Bed headboard',(-.87,-.80,.65),(.87,-.76,1.075),'SIDE',ROUNDING['body'],3)
box('Tailgate',(-.90,-2.53,.45),(.90,-2.48,1.09),'SIDE',ROUNDING['body'],3)
box('Tailgate handle',(-.09,-2.548,.94),(.09,-2.532,.981),'BLACK',.008)
rodeo_emblem('rear')
for x in [-.54,-.36,-.18,0,.18,.36,.54]:box('Bed rib',(x-.018,-2.43,.65),(x+.018,-.83,.667),'BED',.006)
for s in [-1,1]:
    box('Inner bed wall',(s*.78-.065,-2.46,.93),(s*.78+.065,-.82,1.067),'BED',ROUNDING['trim'],3)
    box('Wheel tub',(s*.57-.08,-1.99,.65),(s*.57+.08,-1.01,.92),'BED',.12)
    box('Wheel tub top',(s*.68-.17,-1.99,.92),(s*.68+.17,-1.01,.945),'BED',.012)
    for z in [-2.36,-.89]:beam('Tie down',(s*.79,z,1.045),(s*.84,z,1.045),.021,'CHROME')
for s in [-1,1]:panel('4x4 bed badge',[(s*.954,-2.28,.967),(s*.954,-1.99,.967),(s*.954,-1.99,1.045),(s*.954,-2.28,1.045)],'FOURBY',(s,0,0))
# Chrome front fascia and rectangular headlights with amber corner indicators.
box('Front grille backing',(-.84,2.32,.73),(.84,2.36,1.005),'BLACK',ROUNDING['trim'],3)
for h in [.735,.99]:box('Grille chrome bar',(-.85,2.364,h),(.85,2.394,h+.019),'CHROME')
for h in [.80,.855,.91]:box('Grille slat',(-.45,2.365,h),(.45,2.38,h+.016),'CHROME')
rodeo_emblem('front')
for s in [-1,1]:
    box('Headlight bezel',(s*.65-.19,2.355,.755),(s*.65+.19,2.396,.974),'CHROME',ROUNDING['trim'],3)
    box('Headlight',(s*.635-.13,2.398,.78),(s*.635+.13,2.405,.95),'LAMP',.012,3)
    box('Amber corner',(s*.827-.05,2.39,.785),(s*.827+.05,2.41,.952),'AMBER',.012,3)
    box('Rear lamp',(s*.83-.05,-2.548,.79),(s*.83+.05,-2.531,1.045),'RED',.012,3)
    box('Reverse lamp',(s*.83-.048,-2.553,.755),(s*.83+.048,-2.537,.80),'LAMP')
box('Front chrome bumper',(-.94,2.31,.56),(.94,2.44,.71),'CHROME',ROUNDING['bumper'],3)
box('Front black valance',(-.88,2.29,.43),(.88,2.38,.565),'BLACK',ROUNDING['trim'],3)
for s in [-1,1]:box('Bumper indicator',(s*.58-.12,2.446,.593),(s*.58+.12,2.456,.65),'AMBER',.010,3)
box('Rear bumper',(-.96,-2.62,.49),(.96,-2.49,.59),'CHROME',ROUNDING['bumper'],3)
box('Rear step',(-.57,-2.64,.585),(.57,-2.50,.605),'BLACK',ROUNDING['trim'],3)
# One connected recessed bracket per bumper. The blank floor sits behind the
# future plate plane, with a raised border outside the 305 x 152 mm plate area.
for name in ('front','rear'):
    mount=PLATE_MOUNTS[name];_,height,depth=mount['center'];s=mount['normal'][2]
    vs=[]
    for width,tall,y in [(.356,.198,depth+s*.006),(.322,.170,depth+s*.006),
                         (.322,.170,depth-s*.004),(.356,.198,depth-s*.012)]:
        vs.extend([(x,y,height+z) for x,z in
                   [(-width/2,-tall/2),(width/2,-tall/2),(width/2,tall/2),(-width/2,tall/2)]])
    fs=[]
    for a,c in [(0,4),(4,8),(12,0)]:
        fs.extend((a+i,a+(i+1)%4,c+(i+1)%4,c+i) for i in range(4))
    fs.extend([(8,9,10,11),(15,14,13,12)])
    mount_object=mesh(name+' recessed license plate mount',vs,fs,'BLACK')
    rounded_edges(mount_object,'Plate bracket edge radius',.002,2)
beam('Exhaust outlet',(-.77,-2.34,.37),(-.93,-2.46,.37),.07,'CHROME')
# Full cabin, rear jump cushions, wheel, gauges and left-side pedals.
box('Dashboard',(-.80,.68,.98),(.80,.88,1.12),'DASH',.035)
box('Gauge binnacle',(.22,.67,1.12),(.58,.80,1.19),'BLACK',.015)
panel('Driver instruments',[(.23,.665,1.12),(.57,.665,1.12),(.57,.665,1.18),(.23,.665,1.18)],'GAUGE')
for x in [-.40,.40]:
    box('Seat cushion',(x-.215,-.20,.53),(x+.215,.26,.68),'DASH',.06)
    box('Seat back',(x-.215,-.25,.63),(x+.215,-.15,1.23),'DASH',.05)
    box('Headrest',(x-.12,-.25,1.21),(x+.12,-.15,1.38),'DASH',.025)
    box('Rear jump cushion',(x-.19,-.61,.53),(x+.19,-.37,.65),'DASH',.02)
beam('Gear lever',(0,.38,.49),(0,.34,.76),.028)
box('Shift knob',(-.03,.31,.74),(.03,.37,.80),'BLACK',.02)
beam('4x4 transfer lever',(-.10,.40,.47),(-.10,.36,.64),.022)
bpy.ops.mesh.primitive_torus_add(major_segments=20,minor_segments=5,major_radius=.145,minor_radius=.014,location=(.40,.61,1.13),rotation=(math.pi/2,0,0))
o=bpy.context.object;o.name='Left steering wheel';o.data.materials.append(b.material('BLACK'));active.append(o)
box('Wheel spokes',(.27,.596,1.116),(.53,.624,1.144),'CHROME')
beam('Steering column',(.40,.62,1.13),(.40,.76,1.09),.035)
for x,width in [(.50,.10),(.30,.055)]:box('Driver pedal',(x-width/2,.805,.455),(x+width/2,.831,.565),'CHROME',.006)

def bake(o):
    bpy.context.view_layer.update();me=bpy.data.meshes.new_from_object(o.evaluated_get(bpy.context.evaluated_depsgraph_get()));me.transform(o.matrix_world)
    n=bpy.data.objects.new(o.name+' baked',me);scene.collection.objects.link(n);return n
def uvmap(o):
    uv=o.data.uv_layers.new(name='Grazer atlas')
    for f in o.data.polygons:
        key=o.data.materials[f.material_index].name;x0,y0,x1,y1=REGIONS[key]
        axes=(0,1) if abs(f.normal.z)>.6 else ((1,2) if abs(f.normal.x)>.6 else (0,2))
        points=[o.data.vertices[o.data.loops[i].vertex_index].co for i in f.loop_indices]
        ranges=[(-1.18,1.18),(-2.65,2.46),(.23,1.8)]
        if key=='SIDE':axes=(1,2);ranges[2]=(.40,1.12)
        if key in ['BADGE','GAUGE','LAMP','FOURBY']:
            ranges=[(min(p[a] for p in points),max(p[a] for p in points)) for a in range(3)]
        for li,p in zip(f.loop_indices,points):
            if key.startswith('BADGE_'):
                uv.data[li].uv=((x0+x1)/512,1-(y0+y1)/512)
                continue
            q=[max(0,min(1,(p[a]-ranges[a][0])/max(1e-6,ranges[a][1]-ranges[a][0]))) for a in axes]
            if (key=='BADGE' and p.y<0) or (key=='FOURBY' and p.x>0):q[0]=1-q[0]
            uv.data[li].uv=((x0+2+q[0]*(x1-x0-4))/256,1-(y1-2-q[1]*(y1-y0-4))/256)
def join(name,objs):
    copies=[bake(o) for o in objs]
    for o in copies:
        bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.dissolve_degenerate(bm,dist=1e-7,edges=list(bm.edges));bm.to_mesh(o.data);bm.free()
        uvmap(o)
    bpy.ops.object.select_all(action='DESELECT')
    for o in copies:o.select_set(True)
    bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();o=bpy.context.object;o.name=name
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.triangulate(bm,faces=list(bm.faces))
    bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.calc_area()<1e-9],context='FACES')
    bm.to_mesh(o.data);bm.free()
    return o

# Chrome punched-disc wheels, deep rubber sidewalls and staggered tread blocks.
active=[]
vs=[];fs=[];profile=[(-.13,.275),(-.135,.345),(-.10,.395),(-.075,.405),(.075,.405),(.10,.395),(.135,.345),(.13,.275)]
for x,r in profile:vs.extend([(x,r*math.cos(i*math.tau/32),r*math.sin(i*math.tau/32)) for i in range(32)])
for j in range(len(profile)-1):
    for i in range(32):fs.append((j*32+i,j*32+(i+1)%32,(j+1)*32+(i+1)%32,(j+1)*32+i))
mesh('All terrain tire',vs,fs,'BLACK')
for s in [-1,1]:
    vs=[];fs=[]
    for x,r in [(s*.13,0),(s*.132,.265),(s*.146,.277),(s*.14,.29)]:vs.extend([(x,r*math.cos(i*math.tau/32),r*math.sin(i*math.tau/32)) for i in range(32)])
    for j in range(3):
        for i in range(32):fs.append((j*32+i,j*32+(i+1)%32,(j+1)*32+(i+1)%32,(j+1)*32+i))
    mesh('Chrome disc rim',vs,fs,'CHROME')
    for i in range(8):
        a=i*math.tau/8;cy=.202*math.cos(a);cz=.202*math.sin(a)
        panel('Rim vent',[(s*.149,cy+.035*math.cos(t*math.tau/8),cz+.045*math.sin(t*math.tau/8)) for t in range(8)],'BLACK',(s,0,0))
    vs=[(s*x,r*math.cos(i*math.tau/12),r*math.sin(i*math.tau/12)) for x,r in [(.146,.075),(.181,.075),(.19,.06)] for i in range(12)]
    fs=[(j*12+i,j*12+(i+1)%12,(j+1)*12+(i+1)%12,(j+1)*12+i) for j in range(2) for i in range(12)]+[tuple(range(24,36))]
    mesh('Round locking hub',vs,fs,'CHROME')
    for i in range(6):
        a=i*math.tau/6;y=.11*math.cos(a);z=.11*math.sin(a)
        box('Lug nut',(min(s*.147,s*.17),y-.015,z-.015),(max(s*.147,s*.17),y+.015,z+.015),'CHROME',.006)
for i in range(32):
    a=i*math.tau/32
    for s in [-1,1]:
        o=box('Tread block',(s*.048-.027,-.020,.397),(s*.048+.027,.020,.410),'BLACK',.002)
        o.rotation_euler.x=a
wheel=join('front_wheel',active)
wheel_radius=max(math.hypot(v.co.y,v.co.z) for v in wheel.data.vertices)
for v in wheel.data.vertices:v.co*=WHEELS['radius']/wheel_radius
exports={'body_open':join('Body',fixed),'driver_door':join('Driver door',doors[1]),'passenger_door':join('Passenger door',doors[-1])}
exports.update({name:join(name,[o]) for name,o in panes.items()})
exports['front_wheel']=wheel;exports['rear_wheel']=wheel.copy();exports['rear_wheel'].data=wheel.data.copy();scene.collection.objects.link(exports['rear_wheel'])
MODEL.mkdir(parents=True,exist_ok=True);report={}
for name,o in exports.items():report[name]=export_emesh(o,MODEL/(name+'.emesh'),'rodeo_grazer')
bpy.ops.object.select_all(action='DESELECT')
copies=[]
for name,o in exports.items():
    if 'wheel' in name:continue
    c=o.copy();c.data=o.data.copy();scene.collection.objects.link(c);c.select_set(True);copies.append(c)
bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();closed=bpy.context.object
report['body']=export_emesh(closed,MODEL/'body.emesh','rodeo_grazer');bpy.data.objects.remove(closed,do_unlink=True)
for o in list(scene.objects):
    if o not in exports.values():bpy.data.objects.remove(o,do_unlink=True)
atlas=bpy.data.images.load(str(TEXTURE));atlas.pack()
paint=bpy.data.materials.new('Grazer packed atlas');paint.use_nodes=True;p=paint.node_tree.nodes.get('Principled BSDF');p.inputs['Roughness'].default_value=.42
t=paint.node_tree.nodes.new('ShaderNodeTexImage');t.image=atlas;t.interpolation='Closest';paint.node_tree.links.new(t.outputs['Color'],p.inputs['Base Color'])
rubber=paint.copy();rubber.name='Grazer matte tire rubber'
p=rubber.node_tree.nodes.get('Principled BSDF')
p.inputs['Roughness'].default_value=1.0
p.inputs['Metallic'].default_value=0.0
p.inputs['Specular IOR Level'].default_value=0.0
p.inputs['Coat Weight'].default_value=0.0
glass=bpy.data.materials.new('Grazer tinted windows');glass.use_nodes=True;p=glass.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(.11,.17,.20,1);p.inputs['Alpha'].default_value=.30;p.inputs['Roughness'].default_value=.14;glass.surface_render_method='DITHERED'
for name,o in exports.items():
    o.data.transform(Matrix.Diagonal((-1,1,1,1)))
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
    # Keep the wheel's semantic rubber faces separate from its metal rims.
    rubber_faces=[f.index for f in o.data.polygons
                  if 'wheel' in name and o.data.materials[f.material_index].name=='BLACK']
    o.data.materials.clear();o.data.materials.append(glass if name in panes else paint)
    if rubber_faces:o.data.materials.append(rubber)
    for f in o.data.polygons:f.material_index=0
    for index in rubber_faces:o.data.polygons[index].material_index=1
    if 'wheel' in name:
        o.location=(-.82,1.43 if name.startswith('front') else -1.50,.405)
        c=o.copy();scene.collection.objects.link(c);c.location.x=.82
for name,s in [('driver',-1),('passenger',1)]:
    hinge=bpy.data.objects.new(name+' hinge',None);scene.collection.objects.link(hinge);hinge.location=(s*.90,.92,.43);bpy.context.view_layer.update()
    for o in [exports[name+'_door'],exports[name+'_glass']]:o.parent=hinge;o.matrix_parent_inverse=hinge.matrix_world.inverted()
    for fr,amount in [(1,0),(40,1),(80,1),(120,0)]:hinge.rotation_euler.z=s*math.radians(65)*amount;hinge.keyframe_insert(data_path='rotation_euler',frame=fr)
scene.frame_end=120;scene.frame_set(1)
scene['Identity']='RODEO / GRAZER 4X4 — original compact extended-cab pickup'
scene['Axes']='Blender +Y nose, -X driver left; runtime +Z nose, +X driver left'
for name in ('front','rear'):
    mount=PLATE_MOUNTS[name];x,y,z=mount['center']
    anchor=bpy.data.objects.new(name+' license plate anchor',None)
    scene.collection.objects.link(anchor);anchor.location=(-x,z,y)
    anchor.empty_display_type='CUBE';anchor.empty_display_size=1
    anchor.scale=(PLATE_MOUNTS['width']/2,.001,PLATE_MOUNTS['height']/2)
    anchor['runtime_normal']=mount['normal']
scene['license_plate_mounts']=json.dumps(PLATE_MOUNTS)
bpy.ops.wm.save_as_mainfile(filepath=str(MODEL/'articulated.blend'))
(MODEL/'plate_mounts.json').write_text(json.dumps(PLATE_MOUNTS,indent=2)+'\n')
(MODEL/'cook_report.json').write_text(json.dumps(dict(meshes=report,shape=SHAPE,wheels=WHEELS,driver=DRIVER),indent=2)+'\n')
print('GRAZER COOK',json.dumps(report))
