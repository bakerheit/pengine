"""Blender -b --python tools/cook_kyjhi_payphones.py -- /path/to/kyjhi.psx.payphones
Cook three of the pack's four models: the enclosed booth (phonebooth.fbx),
the open pedestal payphone (payhpone.fbx), and the small wall-mount handset
(phone.fbx). telephone.fbx (a second enclosed box, red-phone-box style) is
not cooked -- one enclosed-booth model is enough.
"""
import bpy, hashlib, json, struct, sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(sys.argv[sys.argv.index('--') + 1])


def cook(fbx_name, texture_name, out_dir_name, label, scale=1.0):
    out = ROOT / 'assets/models/props' / out_dir_name
    out.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    fbx = SOURCE / fbx_name
    bpy.ops.import_scene.fbx(filepath=str(fbx))
    obj = next(o for o in bpy.data.objects if o.type == 'MESH')
    # Blender's imported coordinates are Z-up. Centre the footprint under the
    # model's own bounds so the site placement math drops it straight onto
    # the target point with no per-site offset.
    bb = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    cx = (min(p.x for p in bb) + max(p.x for p in bb)) * .5
    cy = (min(p.y for p in bb) + max(p.y for p in bb)) * .5
    # The vertical axis is NOT recentred -- the source FBX's own Z already
    # carries the model's real mounting height (a floor-standing booth sits
    # at Z=0; the wall-mount handset arrives pre-elevated). The collision box
    # must use that same real vertical centre, not assume the model rests on
    # the ground.
    z_lo, z_hi = min(p.z for p in bb), max(p.z for p in bb)
    box_centre_y = (z_lo + z_hi) * .5 * scale
    half = Vector(((max(p.x for p in bb) - min(p.x for p in bb)) * .5,
                   (z_hi - z_lo) * .5,
                   (max(p.y for p in bb) - min(p.y for p in bb)) * .5)) * scale

    # `scale` shrinks about local (0, 0, 0) -- i.e. about the ground point
    # under the model, not its own centre. A floor-standing model's own
    # Z=0 base is untouched by that (0 * scale == 0), so it stays flush with
    # the ground; only the footprint and the height above the ground shrink.
    # A model whose base sits above Z=0 (a wall mount) would instead slide
    # toward the ground under this scale, so callers pass scale=1.0 for those.
    def local(p): return Vector((p.x - cx, p.z, cy - p.y)) * scale
    def direction(p): return Vector((p.x, p.z, -p.y))

    mesh = obj.data
    mesh.calc_loop_triangles()
    uv = mesh.uv_layers.active
    normal_matrix = obj.matrix_world.to_3x3().inverted().transposed()
    points = [local(obj.matrix_world @ v.co) for v in mesh.vertices]
    reverse = obj.matrix_world.to_3x3().determinant() < 0
    verts = []
    for tri in mesh.loop_triangles:
        loops = list(tri.loops)
        if reverse:
            loops.reverse()
        for idx in loops:
            loop = mesh.loops[idx]
            p = points[loop.vertex_index]
            n = direction(normal_matrix @ loop.normal).normalized()
            tex = uv.data[idx].uv if uv else Vector((0, 0))
            t = n.cross(Vector((0, 1, 0)))
            if t.length < .001:
                t = n.cross(Vector((1, 0, 0)))
            t.normalize()
            verts.append((*p, *n, *tex, *t, 1.0))
    img = bpy.data.images.load(str(SOURCE / texture_name), check_existing=False)
    _ = img.pixels[0]  # Force source decode before changing its output path.
    img.filepath_raw = str(out / 'part_00.png')
    img.file_format = 'PNG'
    img.save()
    with (out / 'part_00.emesh').open('wb') as f:
        name = label.encode() + b'\0'
        f.write(struct.pack('<8I', 0x48534D45, 2, 0, len(verts), len(verts), 1, len(name), 0))
        for v in verts:
            f.write(struct.pack('<12f', *v))
        f.write(struct.pack(f'<{len(verts)}I', *range(len(verts))))
        f.write(struct.pack('<4I', 0, len(verts), 0, 0))
        f.write(name)
    (out / 'materials.txt').write_text('part_00.emesh part_00.png\n')
    (out / 'collision.txt').write_text(
        f'0 {box_centre_y:.5f} 0 {half.x:.5f} {half.y:.5f} {half.z:.5f}\n')
    (out / 'manifest.json').write_text(json.dumps(dict(
        source=f'kyjhi.psx.payphones/{fbx_name}',
        source_sha256=hashlib.sha256(fbx.read_bytes()).hexdigest(),
        transform=f'local=(blender.x-cx, blender.z, cy-blender.y)*{scale}',
        footprint_half_extents=list(half),
        triangles=len(verts) // 3,
    ), indent=2))
    print(label.upper(), len(verts) // 3, 'triangles, half-extents', tuple(half))


# The booth and the pedestal are the two the pack and our own site names call
# "payphone"; both stand on the ground, so scaling them down 25% about local
# (0,0,0) shrinks them in place without lifting or sinking the base. The wall
# handset is mounted above the ground, not on it -- scaling it the same way
# would slide it down the wall -- so it stays at the source FBX's own scale.
cook('phonebooth.fbx', 'Atlas_00002.png', 'kyjhi_phonebooth', 'phonebooth', scale=0.75)
cook('payhpone.fbx', 'payphoneatlas.png', 'kyjhi_payphone', 'payphone', scale=0.75)
cook('phone.fbx', 'payphoneatlas.png', 'kyjhi_wall_phone', 'wall phone')
