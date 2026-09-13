"""Matched front/rear studio views of the Grazer's installed Rodeo emblems."""
import argparse
import sys
from pathlib import Path

import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--model',type=Path,default=ROOT/'assets/models/vehicles/rodeo_grazer/articulated.blend')
parser.add_argument('--output-dir',type=Path,default=ROOT/'build/grazer-emblems')
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
args.output_dir.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(args.model))
scene=bpy.context.scene;scene.frame_set(1)
scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
scene.render.resolution_x=1400;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
if scene.world is None:scene.world=bpy.data.worlds.new('Badge studio')
scene.world.color=(.25,.25,.25);scene.view_settings.view_transform='AgX'
for location,power,size in [((-3,4,5),850,4),((3,-4,4),850,4),((0,0,5),450,3)]:
    light=bpy.data.lights.new('Softbox','AREA');light.energy=power;light.shape='DISK';light.size=size
    obj=bpy.data.objects.new('Softbox',light);scene.collection.objects.link(obj);obj.location=location
    obj.rotation_euler=(Vector((0,0,.9))-obj.location).to_track_quat('-Z','Y').to_euler()
camera=bpy.data.cameras.new('Installed emblem');obj=bpy.data.objects.new('Installed emblem',camera)
scene.collection.objects.link(obj);scene.camera=obj
camera.type='ORTHO';camera.ortho_scale=2.45
for view,side in [('front',1),('rear',-1)]:
    obj.location=(-.95,side*5.8,1.85);target=Vector((0,side*2.25,.88))
    obj.rotation_euler=(target-obj.location).to_track_quat('-Z','Y').to_euler()
    scene.render.filepath=str(args.output_dir/(view+'.png'))
    bpy.ops.render.render(write_still=True)
