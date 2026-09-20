"""Matched native renders of the BWC sedan."""
import bpy,sys,argparse
from pathlib import Path
from mathutils import Vector
parser=argparse.ArgumentParser();parser.add_argument('--view',choices=['front','side','rear','open','ajar','cabin','badge','doors','tail','tail_profile'],default='front');parser.add_argument('--output',required=True)
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
root=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(root/'assets/models/vehicles/bwc_360/articulated.blend'))
scene=bpy.context.scene;scene.frame_set(40 if args.view=='open' else (20 if args.view=='ajar' else 1))
scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
scene.render.resolution_x=1500;scene.render.resolution_y=1050;scene.render.resolution_percentage=100
if scene.world is None:scene.world=bpy.data.worlds.new('Soft grey studio')
scene.world.color=(.21,.21,.21);scene.view_settings.view_transform='AgX'
for location,power,size in [((-3,3,5),950,4),((3,0,4),600,4),((0,-4,4),900,3)]:
    light=bpy.data.lights.new('Studio softbox','AREA');light.energy=power;light.shape='DISK';light.size=size
    obj=bpy.data.objects.new('Studio softbox',light);scene.collection.objects.link(obj);obj.location=location
    obj.rotation_euler=(Vector((0,0,.7))-obj.location).to_track_quat('-Z','Y').to_euler()
views={'tail':((-2.8,-6,1.65),(0,-2.06,.67),2.2),'tail_profile':((-6,-2.8,1.22),(-.48,-2.06,.74),1.45),'doors':((-5,.70,2.0),(-.58,-.12,.84),2.7),'front':((-4,6,2.8),(0,0,.73),5.8),'side':((-7,0,1.9),(0,0,.73),5.65),
       'rear':((-4,-6,2.8),(0,-.12,.73),5.8),'open':((-4,6,3.7),(0,0,.75),6.3),
       'ajar':((-4,6,3.7),(0,0,.75),6.3),'cabin':((-2.8,2.9,2.3),(0,-.05,1.02),2.9),'badge':((-1.2,5,1.8),(0,2.20,.78),1.03)}
eye,target,scale=views[args.view]
camera=bpy.data.cameras.new('BWC preview');obj=bpy.data.objects.new('BWC preview',camera);scene.collection.objects.link(obj);obj.location=eye
obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler();camera.type='ORTHO';camera.ortho_scale=scale;scene.camera=obj
scene.render.filepath=args.output;bpy.ops.render.render(write_still=True)
