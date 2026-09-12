"""Blender: cook the supplied Quequis House GLB into the Church of Waffles.

Run: Blender -b --python tools/cook_church_of_waffles.py -- /path/to/Quequis_House.glb
Private input, textures, meshes and collision stay under ignored assets/models.
The source's surrounding demo city is excluded by an explicit parcel envelope.

This is a SECOND imported-restaurant source, not a BurgerPiz variant: the object
names, the shell's orientation and its lettering are all different, so it gets
its own cooker. It writes exactly the file set city/burgerpiz_asset.h reads —
materials.txt, collision.txt, grounds.txt, lights.txt, parking_lights.txt — so
the loader and the renderer path stay shared.
"""
import bpy
import collections
import json
import math
from pathlib import Path
import struct
import sys
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(sys.argv[sys.argv.index('--') + 1])
BRAND = 'Church of Waffles'
SIGN_MATERIAL = 'ChurchOfWafflesSign'
OUT = ROOT / 'assets/models/buildings/churchofwaffles'

# Whole objects inside this source envelope are kept; everything else is the
# supplied demo city (a gas station, a car wash, a bridge and two hundred
# houses) and is recorded in the manifest's exclusion list instead.
ENVELOPE = ((-20.0, 23.0), (-15.0, 13.5))

# Source metres -> site-local metres. The shell's glazed front faces source +Y
# and the site's frontage faces local +Z, so the horizontal part is a half turn
# about the vertical: local = (-(x - 2.11), z + 0.25, y + 3.33). The pair of
# sign swaps keeps the basis right-handed, so no winding is reversed here.
CENTRE_X = 2.11
CENTRE_Y = -3.33
LIFT = 0.25

# The purple rebrand. Textured source materials keep their base-colour image
# and take a tint; untextured ones are replaced outright. `Y` is the shell's
# original yellow accent band and carries the strongest colour.
PALETTE = {
    'Y': (.42, .12, .72, 1),
    'Plaster': (.80, .70, .94, 1),
    'Plaster_01': (.63, .46, .86, 1),
    'Plaster_02': (.55, .30, .82, 1),
    'Tile': (.72, .58, .92, 1),
    'Roof_tiles': (.50, .31, .72, 1),
    'RooftilesMetal': (.56, .27, .88, 1),
    'brick_wall': (.66, .52, .84, 1),
    'Armchair': (.55, .29, .80, 1),
    'Chair': (.62, .38, .88, 1),
    'Chair_P': (.62, .38, .88, 1),
    'Floor': (.88, .83, .96, 1),
    'FloorsCheckerboard': (.86, .80, .96, 1),
    'Wall_W': (.84, .78, .94, 1),
    'Office_Ceiling': (.92, .89, .98, 1),
    'Metal_01': (.86, .83, .93, 1),
    'Blue': (.36, .18, .78, 1),
}

# These four are painted a strong red in their own base-colour image, so
# multiplying a purple tint through them lands on maroon, not violet: the awning
# fascia and the three upholstery materials. Drop their textures and paint them
# flat, which is what the BurgerPiz-shell brands already do for the same two
# surfaces. Everything else keeps its supplied image and takes the tint.
FLAT = {'RooftilesMetal', 'Armchair', 'Chair', 'Chair_P'}

# The supplied landscaping is alpha-cutout cards, and the imported-restaurant
# render path has no cutout material: every card draws as an opaque black
# rectangle standing in the forecourt. Drop the objects that are nothing but
# foliage cards rather than widen the cooked material format, which the three
# BurgerPiz-shell brands also read. They are listed in the manifest's
# `foliage_cards` so the reason survives with the asset.
FOLIAGE_CARDS = {'Grass', 'Plants_01', 'Plants_02', 'Branches', 'Bush', 'cactus',
                 'plants_flowers'}

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(SOURCE))
OUT.mkdir(parents=True, exist_ok=True)
parking_mat = bpy.data.materials.new('ParkingLampLens')
parking_mat.use_nodes = True

# Replace both original letter meshes with actual extruded type. All shell,
# doorway and furnished interior geometry stays exactly where it was.
# Righteous, the Google face the game already ships and sets its own logo in.
# Using the tracked repo font instead of a macOS system font also means this
# cooker reproduces off a clean checkout without a host font installed.
font = bpy.data.fonts.load(str(ROOT / 'assets/fonts/Righteous-Regular.ttf'))
sign_mat = bpy.data.materials.new(SIGN_MATERIAL)
sign_mat.use_nodes = True
sign_mat.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value = (.88, .74, 1, 1)
# Each row is the source letter mesh being replaced, the centre of the band it
# sat on, and how much of that band the new type may take: 11 m of the 31.6 m
# street face, 9 m of the 12 m east return, and 0.751 m of height either way,
# which is the original band. Widths are generous on purpose so the height is
# what governs — see the fit below.
for old_name, centre, width, height, euler in (
        ('Text', (9.1505, -.332, 4.6435), 11.0, .751, (math.pi/2, 0, math.pi)),
        ('Text.001', (17.328, -5.491, 4.6435), 9.0, .751, (math.pi/2, 0, math.pi/2))):
    bpy.data.objects.remove(bpy.data.objects[old_name], do_unlink=True)
    curve = bpy.data.curves.new(BRAND + ' lettering', 'FONT')
    curve.body = BRAND
    curve.font = font
    curve.align_x = 'CENTER'
    curve.align_y = 'CENTER'
    curve.extrude = .025
    curve.bevel_depth = .004
    # Seventeen characters twice over: at the BurgerPiz cooker's curve
    # resolution this one sign outweighed the whole restaurant at 31k
    # triangles. Two is still round at the size the band actually reads.
    curve.resolution_u = 2
    curve.bevel_resolution = 0
    obj = bpy.data.objects.new(BRAND + ' sign', curve)
    bpy.context.collection.objects.link(obj)
    curve.materials.append(sign_mat)
    bpy.context.view_layer.update()
    # `obj.dimensions` is the object's own bounding box with scale applied and
    # rotation NOT applied, so these axes are the text's: x width, y cap-to-
    # descender height, z extrusion. Fit the width, pull back if that would
    # overflow the band's height, uniformly so the face never skews. For this
    # name in this face the HEIGHT binds at both sizes below — the widths are
    # the budget a longer brand would run into, not what is in force today.
    fit = min(width / obj.dimensions.x, height / obj.dimensions.y)
    obj.scale *= fit
    obj.location = centre
    obj.rotation_euler = euler
    bpy.context.view_layer.update()  # else `dimensions` still predates the fit
    print(f'{old_name} -> {BRAND}: {obj.dimensions.x:.2f} x '
          f'{obj.dimensions.y:.2f} m of type, fit {fit:.3f}')
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.convert(target='MESH')


def local(p):
    return Vector((-(p.x - CENTRE_X), p.z + LIFT, p.y - CENTRE_Y))


def direction(n):
    return Vector((-n.x, n.z, n.y))


groups = collections.defaultdict(list)
boxes = set()
grounds = set()
selected, excluded, foliage, lights, parking_lights = [], [], [], [], []
for obj in sorted(bpy.data.objects, key=lambda o: o.name):
    if obj.type != 'MESH':
        continue
    points = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    lo = Vector([min(p[k] for p in points) for k in range(3)])
    hi = Vector([max(p[k] for p in points) for k in range(3)])
    if not (ENVELOPE[0][0] <= lo.x and hi.x <= ENVELOPE[0][1] and
            ENVELOPE[1][0] <= lo.y and hi.y <= ENVELOPE[1][1]):
        excluded.append(obj.name)
        continue
    if obj.material_slots and all(s.material and s.material.name in FOLIAGE_CARDS
                                 for s in obj.material_slots):
        foliage.append(obj.name)
        continue
    selected.append(obj.name)
    # Prop the single glazed entrance leaf open on its west hinge, folded back
    # flat against the storefront rather than square to it: at ninety degrees
    # the leaf eats a quarter of a 1.06 m opening and a 0.64 m character has
    # 12 cm of play either side. Frame, handle and glazing are all kept, and
    # collision is cooked after the same pose.
    if obj.name == 'Door_Q':
        pivot = Vector((lo.x, hi.y, 0))
        obj.matrix_world = (Matrix.Translation(pivot) @
            Matrix.Rotation(math.radians(150), 4, 'Z') @ Matrix.Translation(-pivot) @ obj.matrix_world)
    mesh = obj.data
    mesh.calc_loop_triangles()
    uv = mesh.uv_layers.active
    normal_matrix = obj.matrix_world.to_3x3().inverted().transposed()
    mirrored = obj.matrix_world.to_3x3().determinant() < 0
    all_points = [local(obj.matrix_world @ v.co) for v in mesh.vertices]
    # Large joined architectural meshes need face-sized wall boxes, otherwise
    # their whole bounds would fill the dining room and erase every doorway.
    # `Cube` is the storefront glazing and `Frame` its mullions; both are one
    # joined mesh wrapping three sides of the dining room.
    #
    # `Counter` is here for the same reason at furniture scale. It is an L —
    # a 9.3 m service run plus a short return leg at its west end — and its
    # bounding box is 9.3 x 4.5 m, so whole-bounds collision fills the service
    # area and seals the staff side of the restaurant off. Its faces are what
    # should stop you, and they leave the east end open to walk around.
    architecture = obj.name in ('Quequis_House', 'Cube', 'Frame', 'Counter')
    for triangle in mesh.loop_triangles:
        mat = obj.material_slots[triangle.material_index].material
        loops = list(triangle.loops)
        if mirrored:
            loops.reverse()
        # The lamp posts put their two downward lens quads, and nothing else,
        # in a dedicated emissive material, so no UV island test is needed.
        lamp_lens = mat.name == 'street_lamps'
        group_name = 'ParkingLampLens' if lamp_lens else mat.name
        tri_points = []
        for idx in loops:
            loop = mesh.loops[idx]
            p = all_points[loop.vertex_index]
            n = direction(normal_matrix @ loop.normal).normalized()
            tex = uv.data[idx].uv if uv else Vector((0, 0))
            tangent = n.cross(Vector((0, 1, 0)))
            if tangent.length < .001:
                tangent = n.cross(Vector((1, 0, 0)))
            tangent.normalize()
            groups[group_name].append((*p, *n, *tex, *tangent, 1.0))
            tri_points.append(p)
        if architecture:
            a = Vector([min(p[k] for p in tri_points) for k in range(3)])
            b = Vector([max(p[k] for p in tri_points) for k in range(3)])
            d = b - a
            if d.y > .12 and (d.x < .025 or d.z < .025):
                centre, half = (a + b) * .5, d * .5
                half.x = max(.035, half.x)
                half.z = max(.035, half.z)
                boxes.add(tuple(round(v, 5) for v in (*centre, *half)))
        if obj.name == 'Floor':
            a = Vector([min(p[k] for p in tri_points) for k in range(3)])
            b = Vector([max(p[k] for p in tri_points) for k in range(3)])
            if b.y - a.y < .002 and b.x - a.x > .01 and b.z - a.z > .01:
                grounds.add(tuple(round(v, 5) for v in
                    ((a.x + b.x) * .5, b.y, (a.z + b.z) * .5, (b.x - a.x) * .5, (b.z - a.z) * .5)))
    a = Vector([min(p[k] for p in all_points) for k in range(3)])
    b = Vector([max(p[k] for p in all_points) for k in range(3)])
    if obj.name.startswith('street_lamps'):
        # One real emitter per head. Both heads live in one object, so cluster
        # the lens faces by shared vertices rather than by object.
        faces = []
        for triangle in mesh.loop_triangles:
            if obj.material_slots[triangle.material_index].material.name != 'street_lamps':
                continue
            faces.append({tuple(round(v, 5) for v in all_points[mesh.loops[i].vertex_index])
                          for i in triangle.loops})
        components = []
        for face in faces:
            joined = [c for c in components if c & face]
            for c in joined:
                components.remove(c)
                face |= c
            components.append(face)
        for component in components:
            clo = Vector([min(p[k] for p in component) for k in range(3)])
            chi = Vector([max(p[k] for p in component) for k in range(3)])
            parking_lights.append(tuple((clo + chi) * .5 - Vector((0, .04, 0))))
    if obj.name.startswith('Light'):
        lights.append(tuple((a + b) * .5 - Vector((0, .10, 0))))
    furniture = obj.name.startswith((
        'Armchair', 'Chair', 'Table', 'Counter', 'Door', 'Frame_Door', 'Grill', 'Oven',
        'Refrigerator', 'Shelf_', 'Shelving', 'Soda_fountain', 'Toilet', 'Wall_W',
        'Washbasin', 'kitchen_hood', 'street_lamps', 'trash_can',
        'cardboard_boxes', 'Box_B', 'Phone', 'FuseBox', 'Base', 'Board'))
    if furniture and not architecture:
        centre, half = (a + b) * .5, (b - a) * .5
        half.x = max(.035, half.x)
        half.y = max(.035, half.y)
        half.z = max(.035, half.z)
        boxes.add(tuple(round(v, 5) for v in (*centre, *half)))

materials = []
for i, (name, verts) in enumerate(sorted(groups.items())):
    stem = f'part_{i:02}'
    mat = bpy.data.materials[name]
    shader = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    color = list(shader.inputs['Base Color'].default_value)
    tex_name = '-'
    color_input = shader.inputs['Base Color']
    # The ceiling panels carry their glow in an emission texture, not a base
    # colour: cook that image, so the lamps read as lit rather than as grey.
    if name == 'Light' and shader.inputs['Emission Color'].is_linked:
        color_input = shader.inputs['Emission Color']
    if color_input.is_linked and name not in FLAT:
        tex = color_input.links[0].from_node
        if tex.type != 'TEX_IMAGE':
            raise RuntimeError(f'Unsupported source base color: {name}')
        tex.image.filepath_raw = str(OUT / (stem + '.png'))
        tex.image.file_format = 'PNG'
        tex.image.save()
        tex_name = stem + '.png'
        color = [1, 1, 1, 1]
    glass = name == 'Glass'
    if glass:
        color = [.86, .90, 1, .32]
    else:
        color = list(PALETTE.get(name, color))
    emissive = name in ('Emissor', 'Light', SIGN_MATERIAL)
    if name == 'ParkingLampLens':
        stem = 'parking_lens'
        tex_name = '-'
        color = [1, 1, 1, 1]
    label = name.encode() + b'\0'
    with (OUT / (stem + '.emesh')).open('wb') as f:
        f.write(struct.pack('<8I', 0x48534D45, 2, 0, len(verts), len(verts), 1, len(label), 0))
        for v in verts:
            f.write(struct.pack('<12f', *v))
        f.write(struct.pack(f'<{len(verts)}I', *range(len(verts))))
        f.write(struct.pack('<4I', 0, len(verts), 0, 0))
        f.write(label)
    materials.append(f'{stem}.emesh {tex_name} {int(glass)} {int(emissive)} ' +
                     ' '.join(str(v) for v in color))
(OUT / 'materials.txt').write_text('\n'.join(materials) + '\n')
(OUT / 'collision.txt').write_text('\n'.join(' '.join(map(str, b)) for b in sorted(boxes)) + '\n')
(OUT / 'grounds.txt').write_text('\n'.join(' '.join(map(str, p)) for p in sorted(grounds)) + '\n')
assert len(parking_lights) == 4, f'Expected four lamp heads, got {parking_lights}'
(OUT / 'parking_lights.txt').write_text(
    '\n'.join(' '.join(map(str, p)) for p in sorted(parking_lights)) + '\n')
(OUT / 'lights.txt').write_text('\n'.join(' '.join(map(str, p)) for p in lights) + '\n')
(OUT / 'manifest.json').write_text(json.dumps(dict(
    source=str(SOURCE), brand='churchofwaffles', display_name=BRAND,
    source_units='metres',
    transform=f'local=(-(source.x - {CENTRE_X}), source.z + {LIFT}, source.y + {-CENTRE_Y})',
    envelope=ENVELOPE, selected=selected, excluded=excluded, foliage_cards=foliage,
    triangles=sum(map(len, groups.values())) // 3, materials=len(materials),
    collision_boxes=len(boxes), grounds=len(grounds), lights=len(lights),
    parking_lights=len(parking_lights)), indent=2))
print('CHURCHOFWAFFLES', len(selected), 'objects', len(foliage), 'foliage cards dropped',
      len(materials), 'materials',
      len(boxes), 'collision boxes', len(grounds), 'grounds',
      len(lights), 'lights', len(parking_lights), 'parking lights')
