"""Render fixed inspection views of the editable 1991 Saddle Tango source."""
from __future__ import annotations

import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from saddle_tango_spec import MODEL, WHEELS

bpy.ops.wm.open_mainfile(filepath=str(MODEL/'source.blend'))
scene=bpy.context.scene
with bpy.data.libraries.load(str(MODEL/'wheel_source.blend')) as (source,dest):
    dest.objects=[name for name in source.objects if name=='WHEEL']
wheel=dest.objects[0]
for side in (-1,1):
    for label,q in (('front',WHEELS['front_z']),('rear',WHEELS['rear_z'])):
        obj=wheel.copy()
        obj.data=wheel.data
        obj.name=f'{label}_{side}_basketweave'
        obj.location=(side*WHEELS['x'],q,WHEELS['arch_y'])
        scene.collection.objects.link(obj)

scene.render.engine='BLENDER_EEVEE'
scene.render.film_transparent=False
scene.render.image_settings.file_format='PNG'
scene.render.resolution_percentage=100
scene.render.resolution_x=1280
scene.render.resolution_y=720
scene.render.engine='BLENDER_EEVEE'
if scene.world is None:
    scene.world=bpy.data.worlds.new('studio_world')
scene.world.color=(.5,.5,.5)

ground=bpy.data.materials.new('studio_floor')
ground.diffuse_color=(.62,.63,.64,1)
bpy.ops.mesh.primitive_plane_add(size=100,location=(0,0,.005))
bpy.context.object.data.materials.append(ground)

def area(name,location,power,size):
    data=bpy.data.lights.new(name,'AREA')
    data.energy=power
    data.shape='DISK'
    data.size=size
    obj=bpy.data.objects.new(name,data)
    scene.collection.objects.link(obj)
    obj.location=location
    obj.rotation_euler=(Vector((0,0,.7))-obj.location).to_track_quat('-Z','Y').to_euler()

area('key',(3.5,4.0,6.0),550,6)
area('rear fill',(-3.0,-4.0,4.0),320,5)
area('side fill',(7.0,0.0,3.0),450,6)

camera_data=bpy.data.cameras.new('inspection_camera')
camera=bpy.data.objects.new('inspection_camera',camera_data)
scene.collection.objects.link(camera)
scene.camera=camera
camera_data.type='ORTHO'
camera_data.ortho_scale=5.75
views={
    'front-three-quarter':(4.5,7.0,3.0),
    'side':(8.0,0,1.05),
    'rear-three-quarter':(4.5,-7.0,3.0),
    'front':(0,8.0,1.15),
    'rear':(0,-8.0,1.15),
}
output=ROOT/'build/saddle-tango-studio'
output.mkdir(parents=True,exist_ok=True)
for name,eye in views.items():
    camera.location=eye
    camera.rotation_euler=(Vector((0,-.2,.72))-camera.location).to_track_quat('-Z','Y').to_euler()
    camera_data.ortho_scale=5.65 if 'quarter' in name or name=='side' else 2.35
    scene.render.filepath=str(output/(name+'.png'))
    bpy.ops.render.render(write_still=True)
area('cabin inspection fill',(0,-.25,1.22),90,1.2)
camera_data.type='PERSP'
camera_data.lens=28
for name,eye,target in (
    ('inside-front-door',(-.38,-.05,1.07),(.87,.12,.64)),
    ('inside-rear-door',(-.34,-.48,1.08),(.86,-.97,.65)),
):
    camera.location=eye
    camera.rotation_euler=(Vector(target)-camera.location).to_track_quat('-Z','Y').to_euler()
    scene.render.filepath=str(output/(name+'.png'))
    bpy.ops.render.render(write_still=True)
print('SADDLE_TANGO_STUDIO',output)
