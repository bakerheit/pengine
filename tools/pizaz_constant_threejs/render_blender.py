"""Import the procedural GLB, save Blender/FBX handoffs, and render fixed QA views."""

from __future__ import annotations

import json
import math
from pathlib import Path

import bpy
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[2]
MODEL = ROOT / "assets/models/vehicles/pizaz_constant"
BUILD = ROOT / "build/pizaz-constant-img2threejs"
GLB = MODEL / "pizaz_constant.glb"

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(GLB))
imported = list(bpy.context.scene.objects)
meshes = [obj for obj in imported if obj.type == "MESH"]
if not meshes:
    raise RuntimeError("The GLB imported with no meshes")

bpy.context.scene.frame_set(0)
bpy.context.view_layer.update()
points = [obj.matrix_world @ Vector(corner) for obj in meshes for corner in obj.bound_box]
lo = [min(p[i] for p in points) for i in range(3)]
hi = [max(p[i] for p in points) for i in range(3)]
triangle_count = sum(sum(len(poly.vertices) - 2 for poly in obj.data.polygons) for obj in meshes)
BUILD.mkdir(parents=True, exist_ok=True)
MODEL.mkdir(parents=True, exist_ok=True)
report = {
    "source": str(GLB),
    "meshCount": len(meshes),
    "triangles": triangle_count,
    "actions": [action.name for action in bpy.data.actions],
    "blenderBounds": {"min": lo, "max": hi},
    "blenderVersion": bpy.app.version_string,
}
(BUILD / "import-report.json").write_text(json.dumps(report, indent=2) + "\n")

for obj in bpy.context.scene.objects:
    obj.select_set(obj in imported)
bpy.context.view_layer.objects.active = meshes[0]
bpy.ops.export_scene.fbx(
    filepath=str(MODEL / "pizaz_constant.fbx"),
    use_selection=True,
    add_leaf_bones=False,
    apply_unit_scale=True,
    bake_anim=True,
)
bpy.ops.wm.save_as_mainfile(filepath=str(MODEL / "pizaz_constant.blend"))

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE"
scene.render.image_settings.file_format = "PNG"
scene.render.resolution_percentage = 100
scene.render.film_transparent = False
scene.world.color = (0.55, 0.56, 0.58)

floor_mat = bpy.data.materials.new("studio grey")
floor_mat.diffuse_color = (0.42, 0.43, 0.45, 1.0)
bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -0.025))
floor = bpy.context.object
floor.name = "preview floor only"
floor.data.materials.append(floor_mat)

def area(name: str, location: tuple[float, float, float], power: float, size: float) -> None:
    data = bpy.data.lights.new(name, "AREA")
    data.energy = power
    data.shape = "DISK"
    data.size = size
    obj = bpy.data.objects.new(name, data)
    scene.collection.objects.link(obj)
    obj.location = location
    direction = Vector((0, 0, 0.6)) - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()

area("large soft key", (3.5, -4.0, 6.0), 420, 6.0)
area("rear fill", (-3.0, 4.0, 4.0), 260, 5.0)
area("side fill", (7.0, 0.0, 2.5), 420, 6.0)

camera_data = bpy.data.cameras.new("QA orthographic camera")
camera = bpy.data.objects.new("QA orthographic camera", camera_data)
scene.collection.objects.link(camera)
scene.camera = camera
camera_data.type = "ORTHO"

# GLB +Y up / +Z forward becomes Blender +Z up / -Y forward.
views = {
    "side": ((8.0, 0.0, 0.66), (0.0, 0.0, 0.66), 5.35, 1280, 720),
    "front": ((0.0, -8.0, 0.66), (0.0, 0.0, 0.66), 2.35, 1024, 768),
    "rear": ((0.0, 8.0, 0.66), (0.0, 0.0, 0.66), 2.35, 1024, 768),
    "top": ((0.0, 0.0, 9.0), (0.0, 0.0, 0.0), 5.05, 768, 1024),
    "three-quarter": ((4.9, -7.2, 3.1), (0.0, 0.0, 0.67), 5.35, 1280, 800),
}
views["rear-three-quarter"] = ((4.9, 7.2, 3.1), (0.0, 0.0, 0.67), 5.35, 1280, 800)
for name, (eye, target, scale, width, height) in views.items():
    camera.location = eye
    direction = Vector(target) - camera.location
    camera.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    if name == "top":
        camera.rotation_euler.z += math.pi
    camera_data.ortho_scale = scale
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.filepath = str(BUILD / f"model-{name}.png")
    bpy.ops.render.render(write_still=True)

scene.frame_set(12)
eye, target, scale, width, height = views["three-quarter"]
camera.location = eye
camera.rotation_euler = (Vector(target) - camera.location).to_track_quat("-Z", "Y").to_euler()
camera_data.ortho_scale = scale
scene.render.resolution_x = width
scene.render.resolution_y = height
scene.render.filepath = str(BUILD / "model-doors-open.png")
bpy.ops.render.render(write_still=True)

print("PIZAZ IMPORT", json.dumps(report))
