"""Blender source audit of the connected cab and usable door apertures.
Run: Blender -b --python-exit-code 1 --python tools/harrow_rearloader_inspect.py
"""
import json,sys
from pathlib import Path
import bpy,bmesh
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'assets/models/vehicles/harrow_rearloader/source.blend'))
cab=bpy.data.objects['Connected rounded cab shell'];door=bpy.data.objects['Driver connected door']
report={}
for obj in (cab,door):
    bm=bmesh.new();bm.from_mesh(obj.data)
    boundary=sum(e.is_boundary for e in bm.edges);nonmanifold=sum(not e.is_manifold for e in bm.edges)
    remaining=set(bm.faces);components=0
    while remaining:
        components+=1;stack=[remaining.pop()]
        while stack:
            f=stack.pop()
            for e in f.edges:
                for neighbor in e.link_faces:
                    if neighbor in remaining:remaining.remove(neighbor);stack.append(neighbor)
    bm.free();report[obj.name]={'boundary_edges':boundary,'nonmanifold_edges':nonmanifold,'connected_components':components}
    assert boundary==0 and nonmanifold==0,(obj.name,'open shell',report[obj.name])
    assert components==1,(obj.name,'disconnected shell',components)
for y,z in ((2.4,2.5),(2.4,1.8)):
    assert not cab.ray_cast(Vector((3,y,z)),Vector((-1,0,0)))[0],'Cab door aperture blocked'
for x in (-.5,.5):
    assert not cab.ray_cast(Vector((x,2.70,2.50)),Vector((0,1,0)))[0],'Windscreen has opaque backing'
report['aperture_rays']='pass: both windshield openings and open door frame show actual cabin'
(ROOT/'build/harrow_rearloader-source-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
