"""Blender -b --python tools/cook_miandi_gas_station.py -- /path/to/Gas_station
Cook the supplied FBX and textures into private, ignored assets/models.
The main FBX already contains the props; the second FBX is a duplicate library.
Use --north-pinatty to cook the independent Six Twelve copy with the three
tracked generated branding textures, preserving the original Miandi output.
"""
import bpy, collections, hashlib, json, math, struct, sys
from pathlib import Path
from mathutils import Matrix, Vector
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(sys.argv[sys.argv.index('--')+1])
NORTH_PINATTY='--north-pinatty' in sys.argv
OUT=ROOT/'assets/models/buildings'/('north_pinatty_gas_station' if NORTH_PINATTY else 'miandi_gas_station')
OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(SOURCE/'Models/Gas_station.fbx'))
# Blender's imported coordinates are Z-up. Preserve metres and handedness.
def local(p): return Vector((p.x-8.5,p.z+.10,7.5-p.y))
def direction(p): return Vector((p.x,p.z,-p.y))
def bounds(points):
 return Vector([min(p[k] for p in points) for k in range(3)]),Vector([max(p[k] for p in points) for k in range(3)])
def record(a,b):return tuple(round(v,5) for v in (*(a+b)*.5,*Vector([max(.035,v*.5) for v in b-a])))
groups=collections.defaultdict(list);boxes=set();grounds=set();lights=[];covers=[];selected=[];excluded=[]
for obj in sorted(bpy.data.objects,key=lambda o:o.name):
 if obj.type!='MESH':continue
 a,b=bounds([obj.matrix_world@Vector(c) for c in obj.bound_box])
 if obj.name.startswith(('Background','Bush','Trees','Road','Ground')) or a.x < -10.1 or b.x > 26.5 or a.y < -19.1 or b.y > 34 or b.z < 0:
  excluded.append(obj.name);continue
 selected.append(obj.name)
 # Prop the customer entrance and restroom entry open; bake the same pose for collision.
 if obj.name in ('Door_03','Door_01'):
  pivot=Vector((a.x,a.y,0))
  obj.matrix_world=Matrix.Translation(pivot)@Matrix.Rotation(-math.pi/2,4,'Z')@Matrix.Translation(-pivot)@obj.matrix_world
 mesh=obj.data;mesh.calc_loop_triangles();uv=mesh.uv_layers.active
 normal_matrix=obj.matrix_world.to_3x3().inverted().transposed()
 points=[local(obj.matrix_world@v.co) for v in mesh.vertices]
 architecture=obj.name in ('6twelve','6twelve.003','Bathrooms','Pillar','Pillar_01','The_ceiling') or obj.name.startswith(('Glass','Window_frame'))
 paving=obj.name.startswith(('Asphalt','Sidewalk','Tiles','Metal_03')) or obj.name in ('6twelve','Bathrooms')
 for tri in mesh.loop_triangles:
  mat=obj.material_slots[tri.material_index].material
  loops=list(tri.loops)
  if obj.matrix_world.to_3x3().determinant()<0:loops.reverse()
  tp=[]
  for idx in loops:
   loop=mesh.loops[idx];p=points[loop.vertex_index];tp.append(p)
   n=direction(normal_matrix@loop.normal).normalized();tex=uv.data[idx].uv if uv else Vector((0,0))
   t=n.cross(Vector((0,1,0)))
   if t.length<.001:t=n.cross(Vector((1,0,0)))
   t.normalize();groups[mat.name].append((*p,*n,*tex,*t,1.0))
  a,b=bounds(tp);d=b-a
  if architecture and d.y>.12 and (d.x<.025 or d.z<.025):boxes.add(record(a,b))
  if paving and d.y<.002 and d.x>.01 and d.z>.01 and b.y<.4:
   grounds.add(tuple(round(v,5) for v in ((a.x+b.x)*.5,b.y,(a.z+b.z)*.5,d.x*.5,d.z*.5)))
 a,b=bounds(points)
 if obj.name.startswith(('Fuel_pump','Shelving','Shelf_Metal','Management','Fridge','Door','ICE','Desk','Chair','Boxs','Dumpster','Sanitary','Marble','Toilet_cubicles')):
  boxes.add(record(a,b))
 if obj.name.startswith('Lamp') or obj.name=='spotlight':lights.append(tuple((a+b)*.5-Vector((0,.16,0))))
 if obj.name in ('The_ceiling','6twelve.003'):
  covers.append(tuple(round(v,5) for v in (*(a+b)*.5,*((b-a)*.5))))
# Ceiling lamps in the supplied mesh include both indoor and canopy fixtures.
textures={p.stem.lower():p for p in (SOURCE/'Textures').iterdir()}
if NORTH_PINATTY:
 textures['6twelve']=ROOT/'assets/textures/world/six_twelve/logo.png'
 textures['6twelve_sign']=ROOT/'assets/textures/world/six_twelve/pylon-atlas.png'
 textures['sign']=ROOT/'assets/textures/world/six_twelve/fascia.png'
 for key in ('6twelve','6twelve_sign','sign'):
  if not textures[key].is_file():raise RuntimeError('Missing generated Six Twelve branding: '+str(textures[key]))
materials=[]
for i,(name,verts) in enumerate(sorted(groups.items())):
 stem=f'part_{i:02}';tex_name='-';color=[1,1,1,1]
 key='6twelve' if name=='6twelve.001' else name.lower()
 glass=name=='Glass';emissive=name in ('Light','Emissor_Verde')
 if key in textures:
  img=bpy.data.images.load(str(textures[key]),check_existing=False)
  _ = img.pixels[0] # Force source decode before changing its output path.
  tex_name=stem+'.png';img.filepath_raw=str(OUT/tex_name);img.file_format='PNG';img.save()
 elif glass:color=[.86,.94,1,.28]
 elif emissive:color=[.2,1,.35,1]
 else:raise RuntimeError('Unmapped material '+name)
 label=name.encode()+b'\0'
 with (OUT/(stem+'.emesh')).open('wb') as f:
  f.write(struct.pack('<8I',0x48534D45,2,0,len(verts),len(verts),1,len(label),0))
  for v in verts:f.write(struct.pack('<12f',*v))
  f.write(struct.pack(f'<{len(verts)}I',*range(len(verts))))
  f.write(struct.pack('<4I',0,len(verts),0,0));f.write(label)
 materials.append(f'{stem}.emesh {tex_name} {int(glass)} {int(emissive)} '+' '.join(map(str,color)))
(OUT/'materials.txt').write_text('\n'.join(materials)+'\n')
for name,data in [('collision',sorted(boxes)),('grounds',sorted(grounds)),('lights',lights),('covers',covers)]:
 (OUT/(name+'.txt')).write_text('\n'.join(' '.join(map(str,p)) for p in data)+'\n')
(OUT/'manifest.json').write_text(json.dumps(dict(brand='Six Twelve' if NORTH_PINATTY else '6twelve',variant='north_pinatty' if NORTH_PINATTY else 'miandi',source='Gas_station.rar / Models/Gas_station.fbx',source_sha256=hashlib.sha256((SOURCE/'Models/Gas_station.fbx').read_bytes()).hexdigest(),transform='local=(blender.x-8.5, blender.z+0.10, 7.5-blender.y)',selected=selected,excluded=excluded,triangles=sum(map(len,groups.values()))//3,materials=len(materials),collision_boxes=len(boxes),lights=len(lights)),indent=2))
print('GAS STATION',len(selected),'objects',len(materials),'materials',len(boxes),'boxes',len(lights),'lights')
