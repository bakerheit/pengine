"""Cook the approved GLB without changing its authored coordinates or source scene."""
import bpy,bmesh,json,sys,math
from pathlib import Path
from mathutils import Matrix,Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from pizaz_constant_spec import ROOT,MODEL,TEXTURE,REGIONS,WHEELS,DRIVER
from vesper_vx91_blender import export_emesh
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(MODEL/'pizaz_constant.glb'))
bpy.context.scene.frame_set(0);bpy.context.view_layer.update()
scene=bpy.context.scene
parts={k:[] for k in ['fixed','driver_door','passenger_door','rear_doors','windshield','rear_glass','driver_glass','passenger_glass','driver_rear_glass','passenger_rear_glass','front_wheel','rear_wheel']}
source_triangles=0
for obj in list(scene.objects):
    if obj.type!='MESH':continue
    ancestry=[];p=obj
    while p:ancestry.append(p.name);p=p.parent
    wheel=next((n for n in ancestry if n in ('driver_front_wheel','driver_rear_wheel','passenger_front_wheel','passenger_rear_wheel')),None)
    if wheel and wheel.startswith('passenger'):continue
    door=next((n for n in ancestry if n.endswith('_door')),None)
    material=obj.data.materials[0]
    key=material.name.removeprefix('pizaz_').split('.')[0]
    if key not in REGIONS:raise RuntimeError(f'Unmapped material {material.name}')
    glass=key=='glass'
    if wheel:part='front_wheel' if '_front_' in wheel else 'rear_wheel'
    elif glass:
        if door:part=door.replace('_front_door','_glass').replace('_rear_door','_rear_glass')
        elif 'windshield' in obj.name:part='windshield'
        elif 'rear_screen' in obj.name:part='rear_glass'
        elif 'fixed_quarter_1' in obj.name:part='driver_rear_glass'
        elif 'fixed_quarter_-1' in obj.name:part='passenger_rear_glass'
        else:raise RuntimeError(f'Unknown glass {obj.name}')
    elif door:part=door.replace('_front_door','_door') if '_front_' in door else 'rear_doors'
    else:part='fixed'
    copy=obj.copy();copy.data=obj.data.copy();scene.collection.objects.link(copy)
    transform=obj.matrix_world.copy()
    if wheel:
        # Custom wheels render about their local X axle; keep original rim design.
        axle=1.38 if part=='front_wheel' else -1.38
        transform=Matrix.Translation(Vector((-.78,axle,-.32)))@transform
    copy.parent=None;copy.matrix_world=Matrix.Identity(4);copy.data.transform(transform)
    copy.data.calc_loop_triangles();source_triangles+=len(copy.data.loop_triangles)
    bpy.context.view_layer.objects.active=copy
    # Remove redundant tessellation while retaining creases and component outlines.
    bm=bmesh.new();bm.from_mesh(copy.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00001)
    bmesh.ops.dissolve_limit(bm,angle_limit=.008,verts=list(bm.verts),edges=list(bm.edges),delimit={'MATERIAL'})
    bm.to_mesh(copy.data);bm.free();copy.data.update()
    if len(copy.data.polygons)>800:
        dec=copy.modifiers.new('Runtime surface reduction','DECIMATE');dec.ratio=.35
        bpy.ops.object.modifier_apply(modifier=dec.name)
    # Shared atlas projection uses the vehicle's global heights, not each panel's bounds.
    for layer in list(copy.data.uv_layers):copy.data.uv_layers.remove(layer)
    uv=copy.data.uv_layers.new(name='RuntimeAtlas')
    x0,y0,x1,y1=REGIONS[key]
    bounds=[(min(v.co[a] for v in copy.data.vertices),max(v.co[a] for v in copy.data.vertices)) for a in range(3)]
    for face in copy.data.polygons:
        for li in face.loop_indices:
            v=copy.data.vertices[copy.data.loops[li].vertex_index].co
            u=max(0,min(1,(v.y+2.35)/4.7));h=max(0,min(1,v.z/1.32))
            if wheel:u=max(0,min(1,(v.y+.32)/.64));h=max(0,min(1,(v.z+.32)/.64))
            elif abs(face.normal.y)>abs(face.normal.x):u=max(0,min(1,(v.x+1.02)/2.04))
            if key in ('lamp','amber','red'):
                # Give small lens faces useful atlas area for filtering and lamp audits.
                a=0 if abs(face.normal.y)>abs(face.normal.x) else 1
                u=(v[a]-bounds[a][0])/max(1e-6,bounds[a][1]-bounds[a][0])
                h=(v.z-bounds[2][0])/max(1e-6,bounds[2][1]-bounds[2][0])
            uv.data[li].uv=((x0+2+u*(x1-x0-4))/256,1-(y1-2-h*(y1-y0-4))/256)
    # Shared exporter swaps Blender Y/Z and reverses winding. Reflect Y first
    # to convert GLB's Blender -Y nose back to runtime +Z without mirroring X.
    copy.data.transform(Matrix.Diagonal((1,-1,1,1)));copy.data.flip_normals()
    parts[part].append(copy)
    if wheel and key!='rubber':
        # Runtime reuses one axle mesh on both sides without mirroring it.
        mirror=copy.copy();mirror.data=copy.data.copy();scene.collection.objects.link(mirror)
        mirror.data.transform(Matrix.Diagonal((-1,1,1,1)));mirror.data.flip_normals()
        parts[part].append(mirror)

def joined(name,objects):
    bpy.ops.object.select_all(action='DESELECT');copies=[]
    for obj in objects:
        c=obj.copy();c.data=obj.data.copy();scene.collection.objects.link(c);c.select_set(True);copies.append(c)
    if not copies:raise RuntimeError(f'Empty part {name}')
    bpy.context.view_layer.objects.active=copies[0]
    if len(copies)>1:bpy.ops.object.join()
    out=bpy.context.object;out.name=name
    bm=bmesh.new();bm.from_mesh(out.data);bmesh.ops.triangulate(bm,faces=list(bm.faces))
    bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.calc_area()<1e-10],context='FACES');bm.to_mesh(out.data);bm.free()
    return out

groups={k:v for k,v in parts.items() if k not in ('fixed','rear_doors')}
groups['body_open']=parts['fixed']
groups['body_drive']=parts['fixed']+parts['rear_doors']
groups['body']=groups['body_drive']+parts['driver_door']+parts['passenger_door']
report={'source_triangles':source_triangles,'wheels':WHEELS,'driver':DRIVER,'parts':{}}
exports=[]
for name,objects in groups.items():
    mesh=joined(name,objects);exports.append(mesh)
    verts,tris=export_emesh(mesh,MODEL/(name+'.emesh'),'pizaz_constant')
    report['parts'][name]={'vertices':verts,'triangles':tris}
# Save derivative separately; never overwrite the approved source or FBX.
for obj in list(scene.objects):
    if obj not in exports:bpy.data.objects.remove(obj,do_unlink=True)
bpy.ops.wm.save_as_mainfile(filepath=str(MODEL/'runtime.blend'))
(ROOT/'build/pizaz-constant-cook.json').write_text(json.dumps(report,indent=2)+'\n')
print('PIZAZ RUNTIME COOK',json.dumps(report))
