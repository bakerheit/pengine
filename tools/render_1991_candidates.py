"""Render the saved editable candidate models; no concept-image substitution."""
import sys,math
from pathlib import Path
import bpy
from mathutils import Vector, Matrix
ROOT=Path(__file__).resolve().parents[1]
args=sys.argv[sys.argv.index('--')+1:]
slug=args[0]
sys.path.insert(0,str(ROOT/'tools'))
from candidate_1991_spec import SHAPES
shape=SHAPES[slug]
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'assets/models/vehicles'/slug/'source.blend'))
scene=bpy.context.scene
scene.render.engine='CYCLES';scene.cycles.samples=32
scene.render.resolution_x=1100;scene.render.resolution_y=780;scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('Studio world')
scene.world.color=(.25,.25,.25)
for obj in bpy.data.objects:
    if obj.type=='MESH' and (' left' in obj.name or ' right' in obj.name):
        obj.hide_render=False;obj.hide_set(False)
# Reproducible ground, framing and studio illumination for every view.
bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.02))
ground=bpy.context.object;ground.name='Preview ground'
mat=bpy.data.materials.new('Preview ground');mat.diffuse_color=(.12,.14,.17,1);ground.data.materials.append(mat)
for location,power,size in (((3,4,7),1600,5),((-4,1,4),1100,4),((1,-5,5),1700,4)):
    bpy.ops.object.light_add(type='AREA',location=location)
    light=bpy.context.object;light.data.energy=power;light.data.shape='DISK';light.data.size=size
    light.rotation_euler=(Vector((0,0,1))-light.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add();cam=bpy.context.object;scene.camera=cam;cam.data.type='PERSP';cam.data.lens=60;cam.data.clip_end=300
scene.view_settings.view_transform='AgX'
scene.view_settings.exposure=-.55
for label,loc,angle in [('front',(6,8,3.4),0),('rear',(6,-8,3.4),0),('side',(10,0,2.6),0),
                        ('side-flat',(10,0,.95),0),('front-flat',(0,10,.95),0),
                        ('rear-flat',(0,-10,.95),0),('top-flat',(0,0,10),0),
                        ('door-partial',(6,8,3.4),28),('door-open',(6,8,3.4),65),('door-inside',(6,-8,3.6),65)]:
    if len(args)>1 and label not in args[1:]:continue
    cam.data.lens=55 if slug=='harrow_hookline' and label in ('rear','door-inside') else 60
    cam.data.type='ORTHO' if label.endswith('-flat') else 'PERSP'
    cam.data.ortho_scale=5.6 if label in ('side-flat','top-flat') else 3.15
    hinge=Vector((shape['half_width'],shape['door_front'],0))
    transform=Matrix.Translation(hinge) @ Matrix.Rotation(math.radians(angle),4,'Z') @ Matrix.Translation(-hinge)
    for obj in bpy.data.objects:
        if obj.get('vehicle_group')=='driver':obj.matrix_world=transform
    target=Vector((0,0,.95 if label.endswith('-flat') else 1.18))
    cam.location=loc;cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
    scene.render.filepath=str(ROOT/'build'/f'{slug}-refined-{label}.png')
    bpy.ops.render.render(write_still=True)
