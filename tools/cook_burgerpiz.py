"""Blender: cook the supplied BurgerPiz GLB, retaining its authored restaurant.

Run: Blender -b --python tools/cook_burgerpiz.py -- /path/to/BurgerPiz.glb
Private input, textures, meshes and collision stay under ignored assets/models.
The source's surrounding demo city is excluded by an explicit parcel envelope.
"""
import bpy
import collections
import json
import math
import shutil
from pathlib import Path
import struct
import sys
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(sys.argv[sys.argv.index('--') + 1])
VARIANT = next((a.split('=',1)[1] for a in sys.argv if a.startswith('--variant=')),
               'tacotaco' if '--tacotaco' in sys.argv else 'burgerpiz')
BRANDS = {'burgerpiz':'BurgerPiz','tacotaco':'TacoTaco',
          'tacomaco':'TacoMaco','freakyfranks':'Freaky Franks'}
if VARIANT not in BRANDS:
    raise ValueError(f'Unknown restaurant variant: {VARIANT}')
BRAND = BRANDS[VARIANT]
SIGN_MATERIAL = BRAND.replace(' ','')+'Sign' 
OUT = ROOT / 'assets/models/buildings' / VARIANT
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(SOURCE))
OUT.mkdir(parents=True, exist_ok=True)
parking_mat=bpy.data.materials.new('ParkingLampLens')
parking_mat.use_nodes=True

if VARIANT in ('tacomaco','freakyfranks'):
    shutil.copyfile(ROOT/'assets/textures/world'/VARIANT/'menu-atlas.png', OUT/'menu_atlas.png')

if VARIANT != 'burgerpiz':
    # Replace the two original letter meshes with actual extruded type.
    # All shell, doorway and furnished interior geometry stays in place.
    font = bpy.data.fonts.load('/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf')
    sign_mat = bpy.data.materials.new(SIGN_MATERIAL)
    sign_mat.use_nodes = True
    sign_mat.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value = (.98,.80,.50,1)
    for old_name,centre,width in (
        ('BurgerPiz_L',( -1.91,3.23,9.64),2.32),
        ('BurgerPiz_L.001',(1.775,4.85,26.98),6.17)):
        bpy.data.objects.remove(bpy.data.objects[old_name],do_unlink=True)
        curve = bpy.data.curves.new(BRAND+' lettering','FONT')
        curve.body=BRAND
        curve.font=font
        curve.align_x='CENTER';curve.align_y='CENTER'
        curve.extrude=.025;curve.bevel_depth=.004;curve.resolution_u=5
        curve.bevel_resolution=0
        obj=bpy.data.objects.new(BRAND+' sign',curve)
        bpy.context.collection.objects.link(obj)
        obj.location=(centre[0],-centre[2],centre[1])
        obj.rotation_euler=(math.pi/2,0,0)
        curve.materials.append(sign_mat)
        bpy.context.view_layer.update()
        obj.scale *= width/obj.dimensions.x
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True);bpy.context.view_layer.objects.active=obj
        bpy.ops.object.convert(target='MESH')

def original(p):
    return Vector((p.x, p.z, -p.y))

def local(p):
    return Vector((10-p.z, p.y+.24, p.x-8))

def direction(p):
    return Vector((-p.z, p.y, p.x))

groups = collections.defaultdict(list)
boxes = set()
grounds = set()
selected, excluded, lights, parking_lights = [], [], [], []
for obj in sorted(bpy.data.objects, key=lambda o:o.name):
    if obj.type != 'MESH':
        continue
    points = [original(obj.matrix_world @ Vector(c)) for c in obj.bound_box]
    lo = Vector([min(p[k] for p in points) for k in range(3)])
    hi = Vector([max(p[k] for p in points) for k in range(3)])
    if not (lo.x >= -10 and hi.x <= 20 and lo.z >= -11 and hi.z <= 31):
        excluded.append(obj.name)
        continue
    selected.append(obj.name)
    # Prop both original entrance leaves open at their outer hinges. Keep all
    # frames, handles and glazing; the collision is cooked after the same pose.
    if obj.name in ('Door_F', 'Door_F.001'):
        hinge_z = lo.z if obj.name == 'Door_F' else hi.z
        pivot = Vector((8.88, -hinge_z, 0))
        angle = math.pi/2 if obj.name == 'Door_F' else -math.pi/2
        obj.matrix_world = (Matrix.Translation(pivot) @
            Matrix.Rotation(angle, 4, 'Z') @ Matrix.Translation(-pivot) @ obj.matrix_world)
    mesh = obj.data
    mesh.calc_loop_triangles()
    uv = mesh.uv_layers.active
    normal_matrix = obj.matrix_world.to_3x3().inverted().transposed()
    mirrored = obj.matrix_world.to_3x3().determinant() < 0
    all_points = [local(original(obj.matrix_world @ v.co)) for v in mesh.vertices]
    # Large joined architectural meshes need face-sized wall boxes, otherwise
    # their whole bounds would fill the dining room and erase every doorway.
    architecture = obj.name == 'BurgerPiz' or obj.name.startswith(('Windows', 'Wood_Base'))
    lens_faces=[]
    for triangle in mesh.loop_triangles:
        mat = obj.material_slots[triangle.material_index].material
        loops = list(triangle.loops)
        if mirrored:
            loops.reverse()
        # The source puts metal and both glass lenses in one texture atlas.
        # Extract only downward faces in its lens UV island, not the arms/pole.
        lamp_lens=(obj.name.startswith('Lamppost') and uv is not None and
            direction(original(normal_matrix @ triangle.normal)).normalized().y < -.98 and
            all(all_points[mesh.loops[i].vertex_index].y > 6 for i in loops) and
            all(.38 <= uv.data[i].uv.x <= .66 for i in loops))
        group_name='ParkingLampLens' if lamp_lens else mat.name
        tri_points = []
        for idx in loops:
            loop = mesh.loops[idx]
            p = all_points[loop.vertex_index]
            n = direction(original(normal_matrix @ loop.normal)).normalized()
            tex = uv.data[idx].uv if uv else Vector((0,0))
            tangent = n.cross(Vector((0,1,0)))
            if tangent.length < .001:
                tangent = n.cross(Vector((1,0,0)))
            tangent.normalize()
            groups[group_name].append((*p, *n, *tex, *tangent, 1.0))
            tri_points.append(p)
        if lamp_lens:
            lens_faces.append({tuple(round(v,5) for v in p) for p in tri_points})
        if architecture:
            a = Vector([min(p[k] for p in tri_points) for k in range(3)])
            b = Vector([max(p[k] for p in tri_points) for k in range(3)])
            d = b-a
            if d.y > .12 and (d.x < .025 or d.z < .025):
                centre, half = (a+b)*.5, d*.5
                half.x=max(.035,half.x);half.z=max(.035,half.z)
                boxes.add(tuple(round(v,5) for v in (*centre,*half)))
        if obj.name == 'Floor' or obj.name.startswith('sidewalk'):
            a = Vector([min(p[k] for p in tri_points) for k in range(3)])
            b = Vector([max(p[k] for p in tri_points) for k in range(3)])
            if b.y-a.y < .002 and b.x-a.x > .01 and b.z-a.z > .01:
                # Flat source paving patches, including the raised entry walk.
                grounds.add(tuple(round(v,5) for v in
                    ((a.x+b.x)*.5,b.y,(a.z+b.z)*.5,(b.x-a.x)*.5,(b.z-a.z)*.5)))
    a = Vector([min(p[k] for p in all_points) for k in range(3)])
    b = Vector([max(p[k] for p in all_points) for k in range(3)])
    # Connected lens faces yield one real emitter per head, even when the
    # source duplicates vertices along UV seams. Position below the glass.
    components=[]
    for face in lens_faces:
        joined=[c for c in components if c & face]
        for c in joined:
            components.remove(c)
            face |= c
        components.append(face)
    for component in components:
        lo=Vector([min(p[k] for p in component) for k in range(3)])
        hi=Vector([max(p[k] for p in component) for k in range(3)])
        parking_lights.append(tuple((lo+hi)*.5-Vector((0,.04,0))))
    if obj.name.startswith('Light'):
        lights.append(tuple((a+b)*.5-Vector((0,.10,0))))
    furniture = obj.name.startswith(('Armchair','Chair','Table','counter','Door','Grill',
        'Shelving','Wall_W','Washbasin','Toilet','Menu','Lamppost')) or obj.name == 'L'
    if furniture and not architecture:
        centre, half = (a+b)*.5,(b-a)*.5
        half.x=max(.035,half.x);half.y=max(.035,half.y);half.z=max(.035,half.z)
        boxes.add(tuple(round(v,5) for v in (*centre,*half)))

materials = []
for i,(name,verts) in enumerate(sorted(groups.items())):
    stem = f'part_{i:02}'
    mat = bpy.data.materials[name]
    shader = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    color = list(shader.inputs['Base Color'].default_value)
    tex_name = '-'
    color_input = shader.inputs['Base Color']
    if name == 'Light' and shader.inputs['Emission Color'].is_linked:
        color_input = shader.inputs['Emission Color']
    if color_input.is_linked:
        tex = color_input.links[0].from_node
        if tex.type != 'TEX_IMAGE':
            raise RuntimeError(f'Unsupported source base color: {name}')
        tex.image.filepath_raw = str(OUT / (stem+'.png'))
        tex.image.file_format = 'PNG'
        tex.image.save()
        tex_name = stem+'.png'
        color = [1,1,1,1]
    glass = name == 'Glass'
    if glass:
        color = [.86,.94,1,.32]
    if VARIANT != 'burgerpiz':
        palette = {
            'Blu':(.025,.36,.29,1),
            'Orange':(.95,.23,.12,1),
            'Plaster_02':(1,.40,.25,1),
            'Plaster_01':(1,.87,.66,1),
            'Plaster':(1,.91,.75,1),
            'BrickPainted':(1,.87,.67,1),
            'RooftilesMetal':(.25,.66,.54,1),
            'Armchair':(.60,.91,.80,1),
        }
        if VARIANT == 'tacomaco':
            palette.update({
                'Blu':(.12,.38,.035,1), 'Orange':(1,.30,.025,1),
                'Plaster_02':(1,.52,.16,1), 'Plaster_01':(1,.94,.72,1),
                'Plaster':(1,.96,.82,1), 'BrickPainted':(1,.94,.76,1),
                'RooftilesMetal':(.35,.72,.10,1), 'Armchair':(.68,.90,.28,1),
            })
        elif VARIANT == 'freakyfranks':
            palette.update({
                'Blu':(.07,.15,.56,1), 'Orange':(.97,.055,.30,1),
                'Plaster_02':(.96,.20,.46,1), 'Plaster_01':(1,.80,.86,1),
                'Plaster':(1,.91,.84,1), 'BrickPainted':(1,.76,.87,1),
                'RooftilesMetal':(.40,.43,.97,1), 'Armchair':(.84,.31,.67,1),
            })
        color=list(palette.get(name,color))
        if name == 'menu_burger':
            if not (OUT/'menu_atlas.png').is_file():
                raise RuntimeError(f'{BRAND} requires its generated menu_atlas.png')
            tex_name='menu_atlas.png'
    emissive = name in ('Emissor', 'Light', SIGN_MATERIAL)
    if name=='ParkingLampLens':
        stem='parking_lens';tex_name='-';color=[1,1,1,1]
    label = name.encode()+b'\0'
    with (OUT/(stem+'.emesh')).open('wb') as f:
        f.write(struct.pack('<8I',0x48534D45,2,0,len(verts),len(verts),1,len(label),0))
        for v in verts:
            f.write(struct.pack('<12f',*v))
        f.write(struct.pack(f'<{len(verts)}I',*range(len(verts))))
        f.write(struct.pack('<4I',0,len(verts),0,0));f.write(label)
    materials.append(f'{stem}.emesh {tex_name} {int(glass)} {int(emissive)} '+
                     ' '.join(str(v) for v in color))
(OUT/'materials.txt').write_text('\n'.join(materials)+'\n')
(OUT/'collision.txt').write_text('\n'.join(' '.join(map(str,b)) for b in sorted(boxes))+'\n')
(OUT/'grounds.txt').write_text('\n'.join(' '.join(map(str,p)) for p in sorted(grounds))+'\n')
assert len(parking_lights)==2, f'Expected both parking lamp heads, got {parking_lights}'
(OUT/'parking_lights.txt').write_text('\n'.join(' '.join(map(str,p)) for p in parking_lights)+'\n')
(OUT/'lights.txt').write_text('\n'.join(' '.join(map(str,p)) for p in lights)+'\n')
(OUT/'manifest.json').write_text(json.dumps(dict(source=str(SOURCE),brand=VARIANT,display_name=BRAND,
    source_units='metres',transform='local=(10-source.z, source.y+0.24, source.x-8)',
    selected=selected,excluded=excluded,triangles=sum(map(len,groups.values()))//3,
    materials=len(materials),collision_boxes=len(boxes),lights=len(lights),parking_lights=len(parking_lights)),indent=2))
print(VARIANT.upper(),len(selected),'objects',len(materials),'materials',len(boxes),'collision boxes')
