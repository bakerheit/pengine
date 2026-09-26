"""Matched studio references plus useful door/cab evidence from the saved source."""
import sys,math
from pathlib import Path
import bpy
from mathutils import Vector,Matrix
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from harrow_rearloader_spec import SHAPE as S
args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'assets/models/vehicles/harrow_rearloader/source.blend'))
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=24
scene.render.threads_mode='FIXED';scene.render.threads=4
scene.render.resolution_x=1400;scene.render.resolution_y=950;scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('Rearloader studio');scene.world.color=(.35,.35,.35)
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.015));ground=bpy.context.object
mat=bpy.data.materials.new('Neutral studio floor');mat.diffuse_color=(.22,.24,.25,1);ground.data.materials.append(mat)
for loc,power,size in (((5,6,9),2200,7),((-5,2,7),1900,6),((2,-7,8),2300,6)):
    bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=power;o.data.shape='DISK';o.data.size=size;o.rotation_euler=(Vector((0,0,1.8))-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add();cam=bpy.context.object;scene.camera=cam;cam.data.clip_end=300;cam.data.lens=58
scene.view_settings.view_transform='AgX';scene.view_settings.exposure=-.25
views=[('front-three-quarter',(10,13,5.5),(0,0,1.8),0,False),('rear-three-quarter',(10,-13,5.5),(0,0,1.8),0,False),('left',(12,0,1.8),(0,0,1.8),0,True),('right',(-12,0,1.8),(0,0,1.8),0,True),('front',(0,14,1.8),(0,0,1.8),0,True),('rear',(0,-14,1.8),(0,0,1.8),0,True),('top',(0,0,15),(0,0,0),0,True),('door-partial',(8,11,4.5),(0,1.0,1.8),28,False),('door-open',(8,11,4.5),(0,1.0,1.8),65,False),('door-inside',(6,-1,3.6),(.65,2.2,1.9),65,False),('cabin',(.02,1.70,2.72),(.1,3.2,1.9),65,False)]
out=ROOT/'docs/design/reviews/harrow-rearloader';out.mkdir(parents=True,exist_ok=True)
for label,loc,target,angle,flat in views:
    if args and label not in args:continue
    cam.location=loc;cam.rotation_euler=(Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO' if flat else 'PERSP';cam.data.lens=24 if label=='cabin' else 58
    cam.data.ortho_scale=8.8 if label in ('left','right') else 11.8 if label=='top' else 5.8
    hinge=Vector((S['driver_hinge'][0],S['driver_hinge'][2],0));t=Matrix.Translation(hinge)@Matrix.Rotation(math.radians(angle),4,'Z')@Matrix.Translation(-hinge)
    for o in bpy.data.objects:
        if o.get('vehicle_group')=='driver':o.matrix_world=t
    scene.render.filepath=str(out/(label+'.png'));bpy.ops.render.render(write_still=True)
