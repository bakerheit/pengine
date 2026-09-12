"""Blender -b --python tools/cook_chain_link_fence.py -- /path/to/psx-modular-chain-link-fence

Cook DanglingBat's PSX modular chain-link fence kit into private, ignored
assets/models/props/chain_link_fence/.

LICENCE, AND WHY NOTHING HERE IS TRACKED. The pack's licence permits use in a
game and forbids redistributing the assets "on their own (even if modified or
edited)". Committing the meshes or the textures to this repository would be
redistribution, so every output lands under assets/models/, which is gitignored
(".gitignore: Proprietary model assets must stay local"). That includes the
TEXTURES, which is the one thing that differs from the rest of this tree: art
normally lives in the tracked assets/textures/. Do not "tidy" them over there.
assets/README.md records the same constraint next to the psx_helicopter note,
which has an unfilled licence gap of its own -- this one is filled and
restrictive, so it is the stricter rule that applies.

WHAT THE KIT IS. Six modules, 126-416 triangles each, 2.0 m square panels on
2.064 m posts, Z-up in metres, sharing two materials: an alpha-cut `chain_link`
diffuse (128x128 with real alpha, which is what makes the mesh read as wire
rather than as a grey sheet) and an opaque `galvanized_steel` for the frame.
The alpha matters: cooked as opaque, a chain-link panel draws as a solid slab.

AXES. Blender imports the glTF Z-up; this engine is Y-up with -Z forward, so
(x, y, z) -> (x, z, -y), the same mapping tools/cook_miandi_gas_station.py
uses. That swap mirrors handedness, so triangle winding is reversed to match --
skip it and every panel is inside-out and invisible under back-face culling.

COLLISION IS A BOX PER PANEL, NOT THE WIRE. The mesh is a flat alpha-cut sheet;
collided per triangle it is a plane the player catches on at grazing angles.
Each module publishes one thin solid box spanning its posts, which is also what
lets game/climb.cpp reason about it: plan_climb() probes for a ledge, and a
mesh with no top face has no ledge to find.

    Blender -b --python tools/cook_chain_link_fence.py -- ~/Downloads/psx-fence
"""
import bpy, hashlib, json, struct, sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/models/props/chain_link_fence'

argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
if not argv:
    raise SystemExit('usage: Blender -b --python tools/cook_chain_link_fence.py -- <kit dir>')
SOURCE = Path(argv[0]).expanduser()
MODELS = next((p for p in SOURCE.rglob('models') if p.is_dir()), None)
if MODELS is None:
    raise SystemExit(f'no models/ directory under {SOURCE}')

OUT.mkdir(parents=True, exist_ok=True)

EMESH_MAGIC = 0x48534D45  # 'EMSH'
EMESH_VERSION = 2


def to_engine(p):
    """Blender Z-up metres -> engine Y-up, -Z forward."""
    return Vector((p.x, p.z, -p.y))


def bounds(points):
    return (Vector([min(p[k] for p in points) for k in range(3)]),
            Vector([max(p[k] for p in points) for k in range(3)]))


def write_emesh(path, verts, label):
    tag = label.encode() + b'\0'
    with path.open('wb') as f:
        f.write(struct.pack('<8I', EMESH_MAGIC, EMESH_VERSION, 0,
                            len(verts), len(verts), 1, len(tag), 0))
        for v in verts:
            f.write(struct.pack('<12f', *v))
        f.write(struct.pack(f'<{len(verts)}I', *range(len(verts))))
        f.write(struct.pack('<4I', 0, len(verts), 0, 0))
        f.write(tag)


saved_textures = {}
manifest = {'source': str(SOURCE.name), 'modules': {}, 'textures': {}}

for glb in sorted(MODELS.glob('*.glb')):
    stem = glb.stem
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(glb))

    groups = {}
    every = []
    for obj in sorted(bpy.data.objects, key=lambda o: o.name):
        if obj.type != 'MESH':
            continue
        mesh = obj.data
        mesh.calc_loop_triangles()
        uv = mesh.uv_layers.active
        normal_matrix = obj.matrix_world.to_3x3().inverted().transposed()
        points = [to_engine(obj.matrix_world @ v.co) for v in mesh.vertices]
        every.extend(points)
        # The Z-up -> Y-up swap mirrors handedness; flip winding back. A
        # negatively scaled object flips it once more.
        flip = obj.matrix_world.to_3x3().determinant() >= 0
        for tri in mesh.loop_triangles:
            material = obj.material_slots[tri.material_index].material
            loops = list(tri.loops)
            if flip:
                loops.reverse()
            for idx in loops:
                loop = mesh.loops[idx]
                p = points[loop.vertex_index]
                n = to_engine(normal_matrix @ loop.normal).normalized()
                tex = uv.data[idx].uv if uv else Vector((0.0, 0.0))
                t = n.cross(Vector((0.0, 1.0, 0.0)))
                if t.length < 1e-3:
                    t = n.cross(Vector((1.0, 0.0, 0.0)))
                t.normalize()
                groups.setdefault(material.name, []).append(
                    (*p, *n, tex[0], tex[1], *t, 1.0))

    if not groups:
        raise SystemExit(f'{glb.name}: no triangles survived the import')

    # One texture per MATERIAL, shared across modules. The kit reuses the same
    # two images everywhere, so writing them once keeps the cook idempotent and
    # the runtime down to two uploads rather than twelve.
    parts = []
    for material_name, verts in sorted(groups.items()):
        image = None
        material = bpy.data.materials[material_name]
        if material.node_tree:
            for node in material.node_tree.nodes:
                if node.type == 'TEX_IMAGE' and node.image and \
                        node.outputs['Color'].is_linked:
                    image = node.image
                    break
        texture_name = '-'
        if image is not None:
            texture_name = material_name + '.png'
            if texture_name not in saved_textures:
                _ = image.pixels[0]   # force decode before repointing output
                image.filepath_raw = str(OUT / texture_name)
                image.file_format = 'PNG'
                image.save()
                saved_textures[texture_name] = tuple(image.size)
                manifest['textures'][texture_name] = {
                    'size': list(image.size),
                    # chain_link carries real alpha; it MUST be uploaded and
                    # drawn alpha-cut or the panel is a solid grey slab.
                    'alpha_cut': material_name == 'chain_link',
                }
        name = f'{stem}__{material_name}'
        write_emesh(OUT / (name + '.emesh'), verts, name)
        parts.append({'mesh': name + '.emesh', 'texture': texture_name,
                      'triangles': len(verts) // 3,
                      'alpha_cut': material_name == 'chain_link'})

    lo, hi = bounds(every)
    size = hi - lo
    # One thin solid box spanning the module, for collision AND for the ledge
    # game/climb.cpp probes for. Kept at the mesh's real footprint so a run of
    # panels butts up with no gap for a character to squeeze through.
    manifest['modules'][stem] = {
        'parts': parts,
        'bounds_min': [round(v, 5) for v in lo],
        'bounds_max': [round(v, 5) for v in hi],
        'size': [round(v, 5) for v in size],
        'collision': [round(v, 5) for v in ((lo + hi) * 0.5)] +
                     [round(max(0.03, v * 0.5), 5) for v in size],
        'sha256': hashlib.sha256(glb.read_bytes()).hexdigest(),
    }
    print(f'{stem:36s} {sum(p["triangles"] for p in parts):5d} tris  '
          f'{size.x:.2f} x {size.y:.2f} x {size.z:.2f} m  '
          f'{len(parts)} parts')

(OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2))
print(f'CHAIN LINK FENCE {len(manifest["modules"])} modules, '
      f'{len(manifest["textures"])} textures -> {OUT.relative_to(ROOT)}')
