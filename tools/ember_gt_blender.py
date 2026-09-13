"""Cook the retained Blender study into a hollow, articulated game car.

Preserves the authored design. Source X rear/Y right/Z up is converted to
the engine convention. Doors are actual separated shell geometry; optical
panes never share the opaque body or frame material.
"""
import bpy, bmesh, math, sys, json, argparse
from pathlib import Path
from mathutils import Vector, Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from ember_gt_spec import MODEL, SOURCE, TEXTURE, COLORS, DRIVER, cell
from vesper_vx91_blender import export_emesh

parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=SOURCE)
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
bpy.ops.wm.open_mainfile(filepath=str(args.source))
scene=bpy.data.scenes['Ember GT | Studio'];bpy.context.window.scene=scene
for ob in scene.objects:ob.hide_set(False)
parts=[];doors={-1:[],1:[]};panes={};wheelparts={}

def mat(key):
    for m in bpy.data.materials:
        if m.name.startswith('Ember | '+key):return m
    m=bpy.data.materials.new('Ember | '+key);m.diffuse_color=(*(a/255 for a in COLORS[key]),1);return m

def newmesh(name,verts,faces,key='Cabin charcoal'):
    me=bpy.data.meshes.new(name);me.from_pydata(verts,[],faces);me.update()
    bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(me);bm.free()
    ob=bpy.data.objects.new(name,me);scene.collection.objects.link(ob);me.materials.append(mat(key));return ob

def cube(name,loc,size,key='Cabin charcoal',bevel=.01):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc);ob=bpy.context.object;ob.name=name;ob.dimensions=size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);ob.data.materials.append(mat(key))
    if bevel:
        m=ob.modifiers.new('Small edge radius','BEVEL');m.width=bevel;m.segments=1
    return ob

def bake(ob):
    if ob.type=='CURVE':
        ob.data.bevel_resolution=0;ob.data.resolution_u=1
    bpy.context.view_layer.update()
    me=bpy.data.meshes.new_from_object(ob.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    me.transform(ob.matrix_world)
    out=bpy.data.objects.new(ob.name+' cooked',me);scene.collection.objects.link(out)
    return out

def clone(ob,name):
    out=ob.copy();out.data=ob.data.copy();out.name=name;scene.collection.objects.link(out);return out

def clip_at_door_back(ob,keep_front):
    bm=bmesh.new();bm.from_mesh(ob.data)
    bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
        dist=1e-6,plane_co=(.60,0,0),plane_no=(1,0,0),
        clear_outer=keep_front,clear_inner=not keep_front)
    bm.to_mesh(ob.data);bm.free()

def boolean(ob,cutter,operation='DIFFERENCE'):
    bpy.context.view_layer.objects.active=ob
    mod=ob.modifiers.new('Physical aperture','BOOLEAN');mod.solver='EXACT';mod.operation=operation;mod.object=cutter
    bpy.ops.object.modifier_apply(modifier=mod.name)

def prism(name,s):
    poly=[(-.885,.24),(-.94,.43),(-.91,.64),(-.825,.792),(.605,.815),(.625,.617),(.485,.316),(.39,.24)]
    n=len(poly);vs=[(x,s*y,z) for y in [.775,1.24] for x,z in poly]
    return newmesh(name,vs,[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)])

def body_shell():
    # Read only the study's shape definitions; do not run its scene builder.
    import ast
    tree=ast.parse((args.source.parent/'build_ember_gt.py').read_text())
    names={'interp','w','fascia_shape','WIDTH','TOP','CROWN'}
    selected=[n for n in tree.body if
        (isinstance(n,ast.FunctionDef) and n.name in names) or
        (isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id in names for t in n.targets))]
    shape={'Vector':Vector}
    exec(compile(ast.Module(body=selected,type_ignores=[]),'<study shape>','exec'),shape)
    vs=[];fs=[];rows=119
    for i in range(rows):
        x=-2.35+4.70*i/(rows-1);wid=shape['w'](x)
        top=shape['interp'](x,shape['TOP']);crown=shape['interp'](x,shape['CROWN'])
        half=[(0,top),(.40,top+.008),(.65,top+(crown-top)*.45),(.83,crown),(.96,crown-.018),
              (1,crown-.065),(.994,.60 if crown>.68 else crown-.1),(.963,.40 if crown>.59 else .31),
              (.97,.25),(.90,.17),(0,.165)]
        ring=[(x,a*wid,z) for a,z in half]+[(x,-a*wid,z) for a,z in reversed(half[1:-1])]
        vs.extend(ring)
    n=len(ring)
    for i in range(rows-1):
        for j in range(n):fs.append((i*n+j,i*n+(j+1)%n,(i+1)*n+(j+1)%n,(i+1)*n+j))
    fs.extend([tuple(reversed(range(n))),tuple((rows-1)*n+j for j in range(n))])
    shell=newmesh('Clean continuous game shell',vs,fs,'Tangerine pearl')
    for face in shell.data.polygons:face.use_smooth=True
    def opening(poly,lo,hi,axis):
        n=len(poly)
        vs=[((a,u,v) if axis=='X' else (u,a,v)) for a in [lo,hi] for u,v in poly]
        faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
        cutter=newmesh('Fascia aperture',vs,faces,'Intake darkness')
        boolean(shell,cutter);bpy.data.objects.remove(cutter,do_unlink=True)
    for side in [-1,1]:
        opening([(.05,.722),(.98,.766),(.86,.655),(.48,.338),(.60,.619)],side*.845,side*1.20,'Y')
        opening([(side*y,z) for y,z in [(.818,.216),(.515,.23),(.55,.387),(.812,.414)]],-2.50,-2.20,'X')
        opening([(side*y,z) for y,z in [(.38,.667),(.83,.621),(.865,.707),(.63,.738),(.40,.713)]],2.21,2.52,'X')
        opening([(side*y,z) for y,z in [(.53,.402),(.86,.397),(.85,.534),(.56,.566)]],2.20,2.52,'X')
    opening([(-.465,.226),(.465,.226),(.412,.367),(-.412,.367)],-2.50,-2.22,'X')
    for v in shell.data.vertices:v.co=shape['fascia_shape'](v.co)
    return shell

source=list(scene.objects)
shell=body_shell()
# Give the shoulder a raised fender crown before cutting the steering pocket.
# Otherwise a wider turning tire punches a slot through the study's low hood.
for v in shell.data.vertices:
    x,y,z=v.co
    if z<.65 or abs(y)<.52:continue
    for axle in [-1.40,1.36]:
        dx=abs(x-axle)
        if dx>=.45:continue
        crown=.390+math.sqrt(.45*.45-dx*dx)
        blend=max(0,min(1,(abs(y)-.52)/.15))
        v.co.z=max(v.co.z,z+(crown-z)*blend)
# Steering clearance below the fender crowns. The top cutoff preserves the
# broad hood while the deeper lower pockets clear the turned tire sidewall.
for x in [-1.40,1.36]:
    for s in [-1,1]:
        bpy.ops.mesh.primitive_cylinder_add(vertices=48,radius=.419,depth=.92,location=(x,s*1.03,.375))
        cylinder=bpy.context.object;cylinder.rotation_euler[0]=math.pi/2
        cylinder.data.materials.append(mat('Intake darkness'))
        bpy.context.view_layer.update()
        cutter=bake(cylinder);bpy.data.objects.remove(cylinder,do_unlink=True)
        limit=cube('Wheel pocket upper limit',(x,s*1.03,.10),(1.0,1.0,1.35),bevel=0)
        boolean(cutter,limit,'INTERSECT')
        boolean(shell,cutter)
        bpy.data.objects.remove(cutter,do_unlink=True);bpy.data.objects.remove(limit,do_unlink=True)
# Remove the solid mass below the studio glazing, retaining a .23 m floor.
cavity=cube('Cockpit volume',(-.105,0,.805),(1.47,1.58,1.15),bevel=0)
boolean(shell,cavity);bpy.data.objects.remove(cavity,do_unlink=True)
cutters={s:prism('Door cut '+str(s),s) for s in [-1,1]}
for s,cutter in cutters.items():
    leaf=clone(shell,'Driver door shell' if s<0 else 'Passenger door shell')
    boolean(leaf,cutter,'INTERSECT');doors[s].append(leaf);boolean(shell,cutter)
parts.append(shell)

skip=('Continuous sculpted','Orange floating roof panel')
old_cabin=('Cockpit tub','Bucket seat','Seat headrest','Dashboard')
moving=('Mirror ','Angular mirror','Flush door','Door lower sculpted')
split=('Knife edge carbon','Intake lower carbon')
for ob in source:
    collections=[c.name[:2] for c in ob.users_collection]
    if not any(c in ['01','02','03','04','05'] for c in collections):continue
    if ob.type not in ['MESH','CURVE','FONT']:continue
    if ob.name.startswith('Continuous sculpted') or ob.name.startswith(old_cabin):continue
    if ob.name.startswith(('Window rubber perimeter','Window lower sill','Door shutline')):continue
    if '03' in collections:
        if 'caliper' in ob.name:
            parts.append(bake(ob))
        elif ob.name.startswith(('Front left','Rear left')):
            wheelparts.setdefault('front' if ob.name.startswith('Front') else 'rear',[]).append(bake(ob))
        continue
    if 'side window' in ob.name:continue
    if ob.name.startswith('Curved panoramic windshield'):
        panes['windshield']=bake(ob);continue
    if ob.name.startswith('Rear engine glass'):
        panes['rear_glass']=bake(ob);continue
    baked=bake(ob)
    if ob.name.startswith('Hood precision'):
        for vertex in baked.data.vertices:
            hit,point,normal,index=shell.ray_cast(Vector((vertex.co.x,vertex.co.y,2)),Vector((0,0,-1)))
            if hit:vertex.co.z=point.z+.008
    if ob.name.startswith(moving):
        center=sum((v.co for v in baked.data.vertices),Vector())/len(baked.data.vertices)
        doors[-1 if center.y<0 else 1].append(baked)
    elif ob.name.startswith(split):
        center=sum((v.co for v in baked.data.vertices),Vector())/len(baked.data.vertices);s=-1 if center.y<0 else 1
        leaf=clone(baked,'Door cooling blade');clip_at_door_back(leaf,True);clip_at_door_back(baked,False)
        if len(leaf.data.polygons):doors[s].append(leaf)
        else:bpy.data.objects.remove(leaf,do_unlink=True)
        if len(baked.data.polygons):parts.append(baked)
        else:bpy.data.objects.remove(baked,do_unlink=True)
    else:parts.append(baked)

for s in [-1,1]:
    name='driver' if s<0 else 'passenger'
    main=[(-.844,s*.761,.79),(-.26,s*.617,1.188),(.52,s*.624,1.186),(.576,s*.802,.837)]
    quarter=[(.54,s*.629,1.177),(.914,s*.787,.943),(.70,s*.808,.824),(.594,s*.804,.834)]
    panes[name+'_glass']=newmesh(name+' moving glass',main,[(0,1,2,3)],'Smoked blue glass')
    panes[name+'_rear_glass']=newmesh(name+' quarter glass',quarter,[(0,1,2,3)],'Smoked blue glass')
    # Solid inner door card leaves no untextured open back when swung outward.
    liner=[(-.845,s*.797,.28),(-.86,s*.797,.70),(.57,s*.797,.74),(.42,s*.797,.30)]
    doors[s].append(newmesh(name+' inner door trim',liner,[(0,1,2,3),(3,2,1,0)]))
    doors[s].append(bake(cube(name+' door armrest',(-.06,s*.789,.56),(.60,.05,.06))))
    doors[s].append(bake(cube(name+' interior release',(-.32,s*.765,.63),(.12,.014,.04),'Interior alloy')))
    # Window base is part of the moving door; A-pillar and roof rail stay fixed.
    doors[s].append(bake(cube(name+' upper sill trim',(-.14,s*.814,.779),(1.30,.025,.025),'Satin carbon',.004)))

# Interior sized for a full-height animated driver in a reclined sports-car pose.
parts.append(bake(cube('Cabin floor',(-.10,0,.245),(1.46,1.57,.035))))
parts.append(bake(cube('Rear cabin bulkhead',(.615,0,.61),(.038,1.52,.73))))
parts.append(bake(cube('Center console',(-.10,0,.405),(1.20,.21,.30))))
parts.append(bake(cube('Dashboard lower',(-.686,0,.655),(.25,1.38,.20))))
parts.append(bake(cube('Dashboard top',(-.70,0,.774),(.27,1.42,.036))))
for s in [-1,1]:
    parts.append(bake(cube('Sport seat cushion',(.16,s*.36,.370),(.50,.39,.11))))
    back=cube('Sport bucket back',(.425,s*.36,.672),(.105,.40,.57));back.rotation_euler[1]=.32;parts.append(bake(back))
    parts.append(bake(cube('Bucket headrest',(.58,s*.36,1.00),(.10,.25,.15))))
    for dy in [-.18,.18]:parts.append(bake(cube('Seat side bolster',(.18,s*.36+dy,.415),(.46,.055,.12),'Cabin stitch')))
driver_y=-DRIVER['left_x']
parts.append(bake(cube('Driver gauge cluster',(-.566,driver_y,.789),(.034,.30,.105),'Gauge face')))
parts.append(bake(cube('Center display',(-.550,0,.771),(.035,.18,.14),'Gauge face')))
wheel_x,wheel_y,wheel_z=DRIVER['wheel']
wheel_source=(-wheel_z,-wheel_x,wheel_y)
bpy.ops.mesh.primitive_torus_add(major_segments=20,minor_segments=6,location=wheel_source,major_radius=.135,minor_radius=.013)
rim=bpy.context.object;rim.name='Steering wheel rim';rim.rotation_euler=(0,math.pi/2,0);rim.data.materials.append(mat('Satin carbon'));parts.append(bake(rim))
parts.append(bake(cube('Steering wheel spokes',wheel_source,(.028,.25,.032),'Interior alloy',.004)))
parts.append(bake(cube('Steering column',(-.51,driver_y,.75),(.16,.045,.045),'Satin carbon',.004)))
# Automatic transmission: wider brake on the left, tall accelerator on the
# right, both ahead of the existing driver ankle targets. No passenger controls.
for control,width,height in [('brake',.115,.105),('accelerator',.060,.140)]:
    x,y,z=DRIVER[control]
    pedal=cube('Driver '+control+' pedal',(-z,-x,y),(.020,width,height),'Interior alloy',.004)
    pedal.rotation_euler.y=-.20
    parts.append(bake(pedal))
    for dz in [-.028,0,.028]:
        grip=cube(control+' pedal grip',(-z+.013,-x,y+dz),(.007,width*.78,.009),'Satin carbon',.001)
        grip.rotation_euler.y=-.20;parts.append(bake(grip))
    parts.append(bake(cube(control+' pedal arm',(-z-.018,-x,.425),(.022,.021,.18),'Satin carbon',.003)))
for cutter in cutters.values():bpy.data.objects.remove(cutter,do_unlink=True)

def join(name,objects):
    objects=[o for o in objects if len(o.data.polygons)]
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:o.select_set(True)
    bpy.context.view_layer.objects.active=objects[0];bpy.ops.object.join();ob=bpy.context.object;ob.name=name;return ob

def reduce(ob,target):
    ob.data.calc_loop_triangles();count=len(ob.data.loop_triangles)
    if count>target:
        bpy.context.view_layer.objects.active=ob
        m=ob.modifiers.new('Runtime silhouette reduction','DECIMATE');m.ratio=target/count;m.use_collapse_triangulate=True
        bpy.ops.object.modifier_apply(modifier=m.name)
    bm=bmesh.new();bm.from_mesh(ob.data)
    bmesh.ops.dissolve_degenerate(bm,dist=1e-7,edges=list(bm.edges))
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(ob.data);bm.free()

body=join('Body | fixed shell and interior',parts)
driver=join('Door | driver',doors[-1]);passenger=join('Door | passenger',doors[1])
reduce(body,10500);reduce(driver,1400);reduce(passenger,1400)
for pane in panes.values():reduce(pane,48)

# Wheels stay separate from the body and attach to the production spin/steer rig.
wheels={}
for axle,objects in wheelparts.items():
    for ob in list(objects):
        if any(k in ob.name for k in ['split spoke','rim outer lip','center cap','lug','hub']):
            mirror=clone(ob,ob.name+' opposite face')
            center_y=-.87 if axle=='front' else -.89
            for v in mirror.data.vertices:v.co.y=2*center_y-v.co.y
            objects.append(mirror)
    wheel=join(axle+' wheel',objects)
    center=Vector((-1.40,-.87,.364) if axle=='front' else (1.36,-.89,.374))
    scale=.365/(.36 if axle=='front' else .37)
    for v in wheel.data.vertices:v.co=(v.co-center)*scale
    reduce(wheel,4400);wheels[axle+'_wheel']=wheel

def key_for(m):
    if m is None:return 'Intake darkness'
    text=m.name.split('Ember | ')[-1]
    for key in COLORS:
        if text.startswith(key):return key
    return 'Cabin charcoal'

def prepare(ob):
    # Export helper maps Blender XYZ -> runtime XZY; first convert study axes.
    for v in ob.data.vertices:
        x,y,z=v.co;v.co=(-y,-x,z)
    bm=bmesh.new();bm.from_mesh(ob.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(ob.data);bm.free()
    for old_uv in list(ob.data.uv_layers):ob.data.uv_layers.remove(old_uv)
    uv=ob.data.uv_layers.new(name='Ember semantic atlas')
    ob.data.uv_layers.active=uv
    for face in ob.data.polygons:
        key=key_for(ob.data.materials[face.material_index]);x,y,_,_=cell(key)
        n=face.normal
        axes=(0,1) if abs(n.z)>.65 else ((1,2) if abs(n.x)>.65 else (0,2))
        for li in face.loop_indices:
            co=ob.data.vertices[ob.data.loops[li].vertex_index].co
            ranges=[(-1.20,1.20),(-2.50,2.50),(.10,1.28)]
            qs=[max(0,min(1,(co[a]-ranges[a][0])/(ranges[a][1]-ranges[a][0]))) for a in axes]
            uv.data[li].uv=((x+3+qs[0]*58)/256,1-(y+61-qs[1]*58)/256)

all_parts={'body_open':body,'driver_door':driver,'passenger_door':passenger,**panes,**wheels}
MODEL.mkdir(parents=True,exist_ok=True)
report={}
for name,ob in all_parts.items():
    prepare(ob);report[name]=export_emesh(ob,MODEL/(name+'.emesh'),'ember_gt')
# The legacy static preview includes its glazing. The actual player loader
# uses body_open plus the separately drawn, transparent optical panes.
closed=join('Body | closed export',[clone(o,o.name+' closed') for o in [body,driver,passenger,*panes.values()]])
report['body']=export_emesh(closed,MODEL/'body.emesh','ember_gt')
bpy.data.objects.remove(closed,do_unlink=True)

# Keep only game objects in this editable file, with actual transparent panes.
keep=set(all_parts.values())
for ob in list(scene.objects):
    if ob not in keep:bpy.data.objects.remove(ob,do_unlink=True)
atlas=bpy.data.images.load(str(TEXTURE),check_existing=True);atlas.pack()
paint=bpy.data.materials.new('Ember GT | game atlas');paint.use_nodes=True
p=paint.node_tree.nodes.get('Principled BSDF');p.inputs['Roughness'].default_value=.44
tex=paint.node_tree.nodes.new('ShaderNodeTexImage');tex.image=atlas;tex.interpolation='Closest';paint.node_tree.links.new(tex.outputs['Color'],p.inputs['Base Color'])
glass=bpy.data.materials.new('Ember GT | semi transparent glass');glass.use_nodes=True
p=glass.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(.09,.16,.19,1);p.inputs['Alpha'].default_value=.28;p.inputs['Roughness'].default_value=.14
glass.surface_render_method='DITHERED';glass.diffuse_color=(.09,.16,.19,.28)
for name,ob in all_parts.items():
    # Export's intermediate X-left/+Y-forward axes are reflected in Blender's
    # Z-up view. Mirror X only AFTER cooking: the editable car then has its
    # wheel, pedals and driver door on the physical left of the +Y nose too.
    ob.data.transform(Matrix.Diagonal((-1,1,1,1)))
    bm=bmesh.new();bm.from_mesh(ob.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(ob.data);bm.free()
    ob.data.materials.clear();ob.data.materials.append(glass if name in panes else paint)
    for f in ob.data.polygons:f.material_index=0
    # Source file stays Z-up with forward Blender +Y. Wheel previews at anchors.
    if name in wheels:
        ob.location=(-.88,1.40 if name.startswith('front') else -1.36,.375)
        opposite=ob.copy();opposite.name=name+' passenger preview'
        scene.collection.objects.link(opposite);opposite.location.x=.88
for name,ob in [('driver',driver),('passenger',passenger)]:
    root=bpy.data.objects.new(name+' door hinge',None);scene.collection.objects.link(root)
    root.location=((-.93 if name=='driver' else .93),.86,.32)
    bpy.context.view_layer.update()
    for child in [ob,panes[name+'_glass']]:
        child.parent=root;child.matrix_parent_inverse=root.matrix_world.inverted()
    root['open_rotation_degrees']=-65 if name=='driver' else 65
    for frame,fraction in [(1,0),(40,1),(80,1),(120,0)]:
        root.rotation_euler.z=math.radians(root['open_rotation_degrees'])*fraction
        root.keyframe_insert(data_path='rotation_euler',frame=frame)
scene.frame_start=1;scene.frame_end=120;scene.frame_set(1)
scene['Game vehicle']='EMBER GT; driver door E, parked passenger door J; transparent panes are separate meshes.'
scene['Authoring axes']='Z up, +Y nose, -X driver left. Runtime export uses +X driver left, Y up, +Z nose.'
bpy.ops.wm.save_as_mainfile(filepath=str(MODEL/'articulated.blend'))
(MODEL/'cook_report.json').write_text(json.dumps(report,indent=2))
print('EMBER_GT_COOK',json.dumps(report))
