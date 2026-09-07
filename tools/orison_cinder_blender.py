#!/usr/bin/env python3
"""Original 1994 Orison Cinder GT. Run in Blender; no fleet changes.

Shape contract (metres, Blender X across / +Y forward / +Z up):
4.42 long, 1.90 body width, 1.245 roof; axles +1.30/-1.24, half-track
.80, wheel centres .34 high and .326 radius. Long front engine hood,
set-back fastback canopy, chamfered nose, closed pop-ups, small bridge wing.
One joined export body, 256px atlas, actual shared wheels in preview only.
"""
from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from vesper_vx91_blender import VehicleBuilder, tangent_for
from orison_cinder_spec import REGIONS, BOUNDS, cell_uv

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/orison-cinder'
ATLAS = ROOT / 'assets/textures/vehicles/orison_cinder/body.png'
MODEL = ROOT / 'assets/models/vehicles/orison_cinder/body.emesh'
AXLES = (-1.24, 1.30)


def interp(y, keys):
    for (a, va), (b, vb) in zip(keys, keys[1:]):
        if y <= b:
            return va + (vb-va) * max(0., (y-a)/(b-a))
    return keys[-1][1]


def deck(y):
    return interp(y, [(-2.21,.79),(-1.60,.865),(-.65,.855),(.55,.83),
                      (1.3,.765),(1.86,.676),(2.21,.615)])


def width(y):
    return interp(y, [(-2.21,.85),(-1.68,.95),(-.8,.935),(.5,.91),
                      (1.3,.935),(1.87,.90),(2.21,.81)])


def shoulder(y):
    crown = max(.095 * max(0, 1-abs(y-a)/.68) for a in AXLES)
    return max(deck(y)+.025, deck(y)+crown)


def set_face_material(b, ob, face, name):
    mat = b.material(name)
    if mat.name not in ob.data.materials:
        ob.data.materials.append(mat)
    face.material_index = ob.data.materials.find(mat.name)


def side_shell(b, s):
    contour = [(-2.21,.16),(-1.86,.16)]
    for axle, ry, rz in [(-1.24,.415,.39),(1.30,.44,.405)]:
        if axle > 0:
            contour += [(-.64,.16),(.61,.16)]
        contour += [(axle-ry,.16),(axle-ry,.34)]
        contour += [(axle-ry*math.cos(math.pi*i/8), .34+rz*math.sin(math.pi*i/8)) for i in range(1,8)]
        contour += [(axle+ry,.34),(axle+ry,.16)]
    contour += [(1.95,.16),(2.21,.16)]
    rings = []
    for y, low in contour:
        w, top = width(y), shoulder(y)
        ring = [(.43,low),(w-.035,low),(w,low+.018),
                (w,top-.045),(w-.075,top),(.43,deck(y))]
        rings.append((y, [(s*x,h) for x,h in ring]))
    ob = b.loft('Continuous fender and sill '+str(s), rings, 'SIDE')
    for f in ob.data.polygons:
        if f.normal.z < -.45:
            set_face_material(b,ob,f,'SHADOW')
        elif f.normal.z > .5:
            set_face_material(b,ob,f,'TOP')


def orient_surface(ob, normal):
    # The shared builder recalculates normals as if every object were a closed
    # volume. Isolated window/fascia sheets need an explicit outward direction.
    bm=bmesh.new();bm.from_mesh(ob.data);bm.normal_update()
    outward=Vector(normal)
    bmesh.ops.reverse_faces(bm,faces=[f for f in bm.faces if f.normal.dot(outward)<0])
    bm.to_mesh(ob.data);bm.free();ob.data.update()
    return ob


def patch(b, name, points, mat, normal):
    return orient_surface(b.panel(name, points, mat, normal),normal)


def export_cinder(body, output):
    """Preserve handedness: Blender (+Y nose,+Z up) -> engine(+Z nose,+Y up).

    Reflecting only Y/Z also mirrors the plate. Negating X makes this a proper
    rotation, so source triangle order and outward normals remain unchanged.
    Existing cars keep their established shared-export convention.
    """
    mesh=body.data;mesh.calc_loop_triangles();uv_layer=mesh.uv_layers.active.data
    vertices=[];indices=[]
    for tri in mesh.loop_triangles:
        points=[];normals=[];uvs=[]
        for index in tri.loops:
            loop=mesh.loops[index];co=mesh.vertices[loop.vertex_index].co
            n=loop.normal.normalized()
            points.append(Vector((-co.x,co.z,co.y)))
            normals.append(Vector((-n.x,n.z,n.y)))
            uvs.append(Vector(uv_layer[index].uv))
        tangent=tangent_for(*points,*uvs,normals[0])
        start=len(vertices)
        for point,normal,uv in zip(points,normals,uvs):
            vertices.append((*point,*normal,*uv,*tangent,1.))
        indices.extend((start,start+1,start+2))
    material=b'orison_cinder\0';output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('wb') as f:
        f.write(struct.pack('<8I',0x48534D45,2,0,len(vertices),len(indices),1,len(material),0))
        for v in vertices:f.write(struct.pack('<12f',*v))
        f.write(struct.pack(f'<{len(indices)}I',*indices))
        f.write(struct.pack('<4I',0,len(indices),0,0));f.write(material)


def framed_quad(b, name, corners, normal, frame='TOP', inset=.055):
    """A glass plane with a real coplanar perimeter, never stacked glass."""
    p = [Vector(v) for v in corners]
    center = sum(p, Vector()) / 4
    inner = [v.lerp(center,inset) for v in p]
    for i in range(4):
        patch(b,name+' frame '+str(i),[p[i],p[(i+1)%4],inner[(i+1)%4],inner[i]],frame,normal)
    return patch(b,name,[tuple(v) for v in inner],'GLASS',normal)


def canopy(b):
    for s in (-1,1):
        rear_low = Vector((s*.835,-1.51,.871))
        front_low = Vector((s*.83,.66,.838))
        rear_top = Vector((s*.635,-.84,1.220))
        front_top = Vector((s*.64,-.045,1.220))
        def pt(u,v):
            return rear_low.lerp(front_low,u).lerp(rear_top.lerp(front_top,u),v)
        # Painted outer perimeter, a thin B pillar, two true glass regions.
        for label,coords,mat in [
            ('sill',[(0,0),(1,0),(1,.07),(0,.07)],'TOP'),
            ('roof rail',[(0,.93),(1,.93),(1,1),(0,1)],'TOP'),
            ('C pillar',[(0,.07),(.09,.07),(.09,.93),(0,.93)],'TOP'),
            ('A pillar',[(.95,.07),(1,.07),(1,.93),(.95,.93)],'TOP'),
            ('quarter glass',[(.09,.07),(.39,.07),(.39,.93),(.09,.93)],'GLASS'),
            ('B pillar',[(.39,.07),(.425,.07),(.425,.93),(.39,.93)],'BLACK'),
            ('door glass',[(.425,.07),(.95,.07),(.95,.93),(.425,.93)],'GLASS'),
        ]:
            patch(b,label+str(s),[pt(u,v) for u,v in coords],mat,(s,0,0))
    framed_quad(b,'Windshield',[(-.83,.66,.838),(.83,.66,.838),
                 (.64,-.045,1.22),(-.64,-.045,1.22)],(0,1,1),inset=.085)
    framed_quad(b,'Fastback rear glass',[(.835,-1.51,.871),(-.835,-1.51,.871),
                 (-.635,-.84,1.22),(.635,-.84,1.22)],(0,-1,1),inset=.10)
    # Broad roof with a small deliberate transverse crown.
    rows = []
    for y,w,h in [(-.84,.635,1.22),(-.65,.65,1.236),(-.23,.65,1.245),(-.045,.64,1.22)]:
        edge_width=interp(y,[(-.84,.635),(-.045,.64)])
        rows.append((y,[(-edge_width,1.22),(-w*.8,h),(w*.8,h),(edge_width,1.22)]))
    verts = [(x,y,z) for y,r in rows for x,z in r]
    faces = [(i*4+j,i*4+j+1,(i+1)*4+j+1,(i+1)*4+j) for i in range(3) for j in range(3)]
    orient_surface(b.add_mesh('Crowned fastback roof',verts,faces,'TOP'),(0,0,1))
    patch(b,'Windshield cowl',[(-.83,.66,deck(.66)),(.83,.66,deck(.66)),
          (.83,.66,.838),(-.83,.66,.838)],'TOP',(0,1,0))


def front_end(b):
    # Faceted bumper shell with an actual recessed central mouth.
    xcols = [-.81,-.63,-.37,.37,.63,.81]
    zrows = [.16,.285,.43,.53,.615]
    for j in range(len(zrows)-1):
        for i in range(len(xcols)-1):
            x0,x1,z0,z1 = xcols[i],xcols[i+1],zrows[j],zrows[j+1]
            if i==2 and j==1:
                mouth=[(x0,2.214,z0),(x1,2.214,z0),(x1,2.214,z1),(x0,2.214,z1)]
                back=[(x*.91,2.125,z+.01 if z==z0 else z-.01) for x,y,z in mouth]
                for k in range(4):
                    patch(b,'Intake depth'+str(k),[mouth[k],mouth[(k+1)%4],back[(k+1)%4],back[k]],'SHADOW',(0,1,0))
                patch(b,'Intake interior',back,'BLACK',(0,1,0))
            else:
                mat='AMBER' if j==2 and i in (0,4) else 'HEADLIGHT' if j==1 and i in (1,3) else 'SIDE'
                patch(b,'Front fascia',[(x0,2.214,z0),(x1,2.214,z0),(x1,2.214,z1),(x0,2.214,z1)],mat,(0,1,0))
    b.loft('Lower front chin',[(2.15,[(-.79,.15),(-.82,.18),(.82,.18),(.79,.15)]),
                              (2.24,[(-.76,.15),(-.79,.18),(.79,.18),(.76,.15)])],'RUBBER')
    # Closed pop-up lid seams are painted into TOP so they sit flush across
    # the hood facets. There are no floating overlay panels here.
    patch(b,'Orison nose emblem',[(-.045,2.04,deck(2.04)+.006),(.045,2.04,deck(2.04)+.006),
                                (.045,2.13,deck(2.13)+.006),(-.045,2.13,deck(2.13)+.006)],'BADGE',(0,0,1))


def rear_end(b):
    # Recessed full-width black lamp surround, two separated red clusters.
    xs=[-.85,-.77,-.30,.30,.77,.85]
    zs=[.16,.31,.49,.675,.79]
    for j in range(4):
        for i in range(5):
            x0,x1,z0,z1=xs[i],xs[i+1],zs[j],zs[j+1]
            mat='SIDE'
            if j==2:
                mat='RED' if i in (1,3) else 'PLATE' if i==2 else 'AMBER'
            if j==0: mat='SHADOW'
            patch(b,'Rear lamp and bumper receiver',[(x1,-2.214,z0),(x0,-2.214,z0),
                     (x0,-2.214,z1),(x1,-2.214,z1)],mat,(0,-1,0))
    for s in (-1,1):
        a,c=sorted((s*.34,s*.43))
        patch(b,'Reverse inset',[(a,-2.218,.525),(c,-2.218,.525),(c,-2.218,.558),(a,-2.218,.558)],'REVERSE',(0,-1,0))
        a,c=sorted((s*.575,s*.625))
        b.box('Short wing pedestal',(a,-1.955,.817),(c,-1.79,.968),'TOP')
    wing=[(-.86,.963),(-.83,1.015),(.83,1.015),(.86,.963)]
    b.loft('Low bridge rear wing',[(-2.055,wing),(-1.76,[(-.84,.955),(-.82,.976),(.82,.976),(.84,.955)])],'TOP')
    for x in (-.58,.58):
        b.cylinder('Exhaust tip',(x,-2.225,.25),.047,.13,8,'METAL')
        points=[(x+.035*math.cos(i*math.tau/8),-2.295,.25+.035*math.sin(i*math.tau/8)) for i in range(8)]
        patch(b,'Exhaust bore',points,'BLACK',(0,-1,0))


def mirrors(b):
    for s in (-1,1):
        a,c=sorted((s*.82,s*.96))
        b.box('Mirror stem',(a,.40,.857),(c,.47,.892),'BLACK')
        ob=b.loft('Body colour mirror',[(.34,[(s*.91,.86),(s*.925,.947),(s*1.065,.938),(s*1.07,.875)]),
                       (.53,[(s*.91,.858),(s*.925,.92),(s*1.045,.913),(s*1.05,.872)])],'TOP')
        for face in ob.data.polygons:
            if face.normal.y < -.8: set_face_material(b,ob,face,'METAL')


def assign_uvs(ob):
    uv=ob.data.uv_layers.new(name='Cinder_256')
    for face in ob.data.polygons:
        key=ob.data.materials[face.material_index].name
        name=key.split('.')[0]
        points=[ob.data.vertices[ob.data.loops[i].vertex_index].co for i in face.loop_indices]
        n=face.normal
        axes=(0,1) if abs(n.z)>=max(abs(n.x),abs(n.y)) else (0,2) if abs(n.y)>abs(n.x) else (1,2)
        if name=='SIDE': axes=(1,2)
        if name=='TOP': axes=(0,1)
        all_points=[v.co for v in ob.data.vertices]
        bounds=BOUNDS.get(name,tuple((min(p[a] for p in all_points),max(p[a] for p in all_points)) for a in axes))
        x0,y0,x1,y1=REGIONS[name]
        for i,p in zip(face.loop_indices,points):
            ab=[max(0,min(1,(p[a]-lo)/(hi-lo))) if hi-lo>1e-7 else .5 for a,(lo,hi) in zip(axes,bounds)]
            uv.data[i].uv=cell_uv(name,*ab)


def textured_material(name, image, roughness=.48, metal=.12):
    mat=bpy.data.materials.new(name);mat.use_nodes=True
    sh=mat.node_tree.nodes.get('Principled BSDF')
    sh.inputs['Roughness'].default_value=roughness
    sh.inputs['Metallic'].default_value=metal
    node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=image;node.interpolation='Closest'
    mat.node_tree.links.new(node.outputs['Color'],sh.inputs['Base Color'])
    return mat


def build_body(blockout=False):
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    for m in list(bpy.data.materials): bpy.data.materials.remove(m)
    b=VehicleBuilder()
    b.box('Central chassis',(-.38,-2.20,.16),(.38,2.08,.30),'SHADOW')
    for s in (-1,1): side_shell(b,s)
    stations=sorted(set([-2.21,-1.6,-.65,.55,1.3,1.86,2.08,2.10,2.21]))
    b.loft('Continuous central deck',[(y,[(-.43,.455 if y>=2.10 else .30),
        (-.43,deck(y)),(.43,deck(y)),(.43,.455 if y>=2.10 else .30)]) for y in stations],'TOP')
    canopy(b)
    if not blockout:
        front_end(b);rear_end(b);mirrors(b)
    for ob in b.objects:
        # Geometry groups survive joining, so the source stays easy to edit.
        group=ob.vertex_groups.new(name=ob.name)
        group.add(list(range(len(ob.data.vertices))),1.0,'REPLACE')
        bm=bmesh.new();bm.from_mesh(ob.data)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7)
        bmesh.ops.triangulate(bm,faces=list(bm.faces))
        bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.calc_area()<1e-9],context='FACES')
        bm.to_mesh(ob.data);bm.free();ob.data.update()
        assign_uvs(ob)
    body=b.join();body.name='BODY | Orison Cinder GT';body.data.name='OrisonCinderGT_1994'
    assert len([o for o in bpy.context.scene.objects if o.type=='MESH'])==1, 'body export must contain one mesh and no wheels'
    if not blockout:
        export_cinder(body,MODEL)
    atlas=bpy.data.images.load(str(ATLAS),check_existing=True);atlas.pack()
    mat=textured_material('Cinder GT | single 256px atlas',atlas)
    body.data.materials.clear();body.data.materials.append(mat)
    for f in body.data.polygons:f.material_index=0
    if blockout:
        # Silhouette pass: neutral paint, dark glass remains easy to read via atlas.
        mat.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value=.72
    body['make']='Orison';body['model']='Cinder GT';body['year']=1994
    body['body_only_export']=str(MODEL.relative_to(ROOT))
    body['axis_convention']='Blender: +Y nose, +Z up; engine: +Z nose, +Y up'
    return body


def preview_wheels():
    col=bpy.data.collections.new('02 | Shared wheels - preview only');bpy.context.scene.collection.children.link(col)
    data=(ROOT/'assets/models/vehicles/common/wheel.emesh').read_bytes()
    header=struct.unpack_from('<8I',data);nv,ni=header[3:5]
    verts=[struct.unpack_from('<12f',data,32+48*i) for i in range(nv)]
    inds=struct.unpack_from(f'<{ni}I',data,32+48*nv)
    radius=max(max(v[a] for v in verts)-min(v[a] for v in verts) for a in (1,2))*.5
    scale=.326/radius
    mesh=bpy.data.meshes.new('Apricot shared wheel')
    mesh.from_pydata([(v[0]*scale,v[2]*scale,v[1]*scale) for v in verts],[],
                     [(inds[i],inds[i+2],inds[i+1]) for i in range(0,ni,3)])
    mesh.update();uv=mesh.uv_layers.new(name='Wheel UV')
    for face in mesh.polygons:
        for loop in face.loop_indices: uv.data[loop].uv=verts[mesh.loops[loop].vertex_index][6:8]
    im=bpy.data.images.load(str(ROOT/'assets/textures/vehicles/common/wheel.png'));im.pack()
    mesh.materials.append(textured_material('Shared wheel atlas',im,.82,.02))
    for s in (-1,1):
        for label,y in [('F',1.30),('R',-1.24)]:
            anchor=bpy.data.objects.new('WHEEL_'+label+('L' if s<0 else 'R'),None)
            anchor.location=(s*.80,y,.34);anchor.empty_display_size=.11
            bpy.context.scene.collection.objects.link(anchor)
            ob=bpy.data.objects.new('Preview '+anchor.name,mesh);col.objects.link(ob)
            ob.parent=anchor
    return col


def point_at(ob, target):
    ob.rotation_euler=(Vector(target)-ob.location).to_track_quat('-Z','Y').to_euler()


def studio(body):
    scene=bpy.context.scene
    stage=bpy.data.collections.new('03 | Studio - excluded from body export');scene.collection.children.link(stage)
    def move_to_stage(ob):
        for c in list(ob.users_collection):c.objects.unlink(ob)
        stage.objects.link(ob)
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,.004))
    ground=bpy.context.object;ground.name='Studio floor';move_to_stage(ground)
    mat=bpy.data.materials.new('Studio warm graphite');mat.diffuse_color=(.045,.057,.064,1);mat.use_nodes=True
    mat.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.045,.057,.064,1)
    mat.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value=.82
    ground.data.materials.append(mat)
    for name,loc,energy,size,color in [
        ('Large key',(-3.7,2.9,6),1350,5,(.80,.92,1)),
        ('Soft fill',(4.2,1.0,3.6),1000,4,(1,.86,.70)),
        ('Roof strip',(-.5,-3.8,4.7),1600,3,(.70,.90,1)),
    ]:
        dat=bpy.data.lights.new(name,'AREA');dat.energy=energy;dat.shape='DISK';dat.size=size;dat.color=color
        ob=bpy.data.objects.new(name,dat);stage.objects.link(ob);ob.location=loc;point_at(ob,(0,0,.5))
    camdata=bpy.data.cameras.new('Design camera');cam=bpy.data.objects.new('Design camera',camdata);stage.objects.link(cam)
    scene.camera=cam;camdata.type='ORTHO';camdata.ortho_scale=5.6
    scene.render.engine='CYCLES';scene.cycles.samples=32
    scene.cycles.use_denoising=True
    scene.render.resolution_x=1440;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
    scene.world.color=(.22,.22,.22)
    scene.view_settings.view_transform='AgX'
    scene.render.image_settings.file_format='PNG'
    scene.render.film_transparent=False
    scene.unit_settings.system='METRIC'
    scene.render.fps=24
    bpy.ops.object.select_all(action='DESELECT');body.select_set(True);bpy.context.view_layer.objects.active=body
    cam.location=(-5.4,6.6,3.5);point_at(cam,(0,0,.62))
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_distance=6.2
                area.spaces.active.region_3d.view_location=Vector((0,0,.62))
                area.spaces.active.region_3d.view_rotation=cam.rotation_euler.to_quaternion()
                area.spaces.active.shading.type='MATERIAL'
                area.spaces.active.overlay.show_overlays=False
                area.spaces.active.clip_end=300
    return cam


def main():
    p=argparse.ArgumentParser();p.add_argument('--blockout',action='store_true');p.add_argument('--skip-renders',action='store_true')
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    OUT.mkdir(parents=True,exist_ok=True)
    body=build_body(args.blockout);preview_wheels();cam=studio(body)
    name='blockout' if args.blockout else 'orison_cinder_gt_1994'
    readme=bpy.data.texts.new('READ ME | Orison Cinder GT')
    readme.write('1994 ORISON CINDER GT\nOriginal fictional coupe designed for Apricot.\n\n'
      'BODY is one joined mesh with named vertex groups for major parts.\n'
      'Shared wheels are separate preview objects parented to four anchors.\n'
      'Body atlas and wheel texture are packed into this file.\n'
      'Body: +Y nose, +Z up; dimensions in metres.\n'
      'Rebuild: Blender --background --factory-startup --python tools/orison_cinder_blender.py\n'
      'Playable: F1 > Vehicle > Choose Car > Orison > Cinder GT.\n')
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(name+'.blend')))
    if not args.skip_renders:
        for view,loc,scale in [('front',(-5.4,6.6,3.5),5.5),('side',(-7.5,0,1.0),5.3),
                               ('rear',(-5.3,-6.3,3.1),5.5),('above',(-3.8,6.4,6.0),5.4)]:
            cam.location=loc;cam.data.ortho_scale=scale;point_at(cam,(0,0,.63))
            bpy.data.objects['Studio floor'].hide_render=(view=='side')
            bpy.context.scene.render.filepath=str(OUT/(name+'-'+view+'.png'))
            bpy.ops.render.render(write_still=True)
    print('CINDER_COMPLETE',json.dumps({'blend':str(OUT/(name+'.blend')),
          'body_triangles':len(body.data.polygons),'body_vertices':len(body.data.vertices)}))


if __name__=='__main__': main()
