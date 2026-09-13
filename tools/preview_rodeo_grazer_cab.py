"""Blender close-up QA for the Grazer roof, window returns and door joins."""
import argparse
import sys
from pathlib import Path
import bpy
from mathutils import Vector

parser=argparse.ArgumentParser()
root=Path(__file__).resolve().parents[1]
parser.add_argument('--model',default=str(root/'assets/models/vehicles/rodeo_grazer/articulated.blend'))
parser.add_argument('--output',default=str(root/'build/grazer-seams-close.png'))
parser.add_argument('--open',action='store_true')
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
bpy.ops.wm.open_mainfile(filepath=args.model)
scene=bpy.context.scene;scene.frame_set(40 if args.open else 1)
scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
scene.render.resolution_x=1400;scene.render.resolution_y=1050;scene.render.resolution_percentage=100
if scene.world is None:scene.world=bpy.data.worlds.new('Neutral studio')
scene.world.color=(.25,.25,.25)
scene.view_settings.view_transform='AgX'
for location,power,size in [((-3,2,5),750,4),((3,1,4),500,3),((0,-3,4),600,3)]:
    light=bpy.data.lights.new('Softbox','AREA');light.energy=power;light.shape='DISK';light.size=size
    obj=bpy.data.objects.new('Softbox',light);scene.collection.objects.link(obj);obj.location=location
    obj.rotation_euler=(Vector((0,0,1))-obj.location).to_track_quat('-Z','Y').to_euler()
camera=bpy.data.cameras.new('Cab detail');obj=bpy.data.objects.new('Cab detail',camera);scene.collection.objects.link(obj)
obj.location=(-2.8,2.65,2.42);target=Vector((-.24,.06,1.35))
obj.rotation_euler=(target-obj.location).to_track_quat('-Z','Y').to_euler()
camera.type='ORTHO';camera.ortho_scale=2.5 if not args.open else 3.7
scene.camera=obj;scene.render.filepath=args.output
bpy.ops.render.render(write_still=True)
