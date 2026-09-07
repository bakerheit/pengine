#!/usr/bin/env python3
"""Shared reproducible semi/body builder; exported bodies contain no wheels."""
import argparse
import importlib
import math
import sys
from pathlib import Path
import bpy
import bmesh
sys.path.insert(0,str(Path(__file__).resolve().parent))
from vesper_vx91_blender import VehicleBuilder, export_emesh


def assign_face_material(b,obj,axis,coordinate,material):
    obj.data.materials.append(b.material(material))
    for f in obj.data.polygons:
        if all(abs(obj.data.vertices[i].co[axis]-coordinate)<1e-5 for i in f.vertices):
            f.material_index=obj.data.materials.find(material)


def fender(b,side,axle,inner=.64,outer=.74):
    # A broad, solid crown meets the hood; actual empty space below the arch.
    vs=[]
    for i in range(13):
        a=i*math.pi/12
        for x,r in ((.62,outer),(1.18,outer),(1.18,inner),(.62,inner)):
            vs.append((side*x,axle+math.cos(a)*r,.5+math.sin(a)*r))
    faces=[]
    for i in range(12):
        for j in range(4): faces.append((i*4+j,i*4+(j+1)%4,(i+1)*4+(j+1)%4,(i+1)*4+j))
    faces.extend([(3,2,1,0),(48,49,50,51)])
    return b.add_mesh('OpenFender',vs,faces,'PAINT')


def tractor(b):
    b.box('CentralFrame',(-.48,-3.3,.42),(.48,3.13,.65),'DARK')
    for s in (-1,1):
        lo,hi=sorted((s*.48,s*.60))
        b.box('FrameRail',(lo,-3.26,.65),(hi,1.12,.83),'DARK')
        fender(b,s,2.05)
        fender(b,s,-2.05,.59,.65)
    hood=b.loft('RaisedTaperedHood',[(z,[(-w,1.23),(-w,h-.13),(-w*.83,h),
                      (w*.83,h),(w,h-.13),(w,1.23)]) for z,w,h in
                      ((1.32,.83,1.80),(2.30,.78,1.73),(3.15,.71,1.65))],'TOP')
    assign_face_material(b,hood,1,3.15,'FRONT')
    grille=b.box('GrilleReceiver',(-.66,3.13,.66),(.66,3.15,1.61),'DARK')
    assign_face_material(b,grille,1,3.15,'FRONT')
    cab=b.loft('CabShell',[(z,[(-1.06,1.10),(-1.10,2.15),(-.96,roof-.12),
                    (-.84,roof),(.84,roof),(.96,roof-.12),(1.10,2.15),(1.06,1.10)])
                    for z,roof in ((.05,3.13),(.22,3.25),(.85,3.25),(1.40,2.85))],'SIDE')
    assign_face_material(b,cab,1,.05,'REAR')
    # Directly assign the upper sloped front strip to split windshield texture.
    cab.data.materials.append(b.material('FRONT'))
    cab.data.materials.append(b.material('TOP'))
    for f in cab.data.polygons:
        ps=[cab.data.vertices[i].co for i in f.vertices]
        if min(p.y for p in ps)>=.8499 and min(p.z for p in ps)>2.7:
            f.material_index=cab.data.materials.find('FRONT')
        elif min(p.z for p in ps)>3.1:
            f.material_index=cab.data.materials.find('TOP')
    assign_face_material(b,cab,1,1.40,'FRONT')
    b.box('RearDeck',(-.70,-2.77,1.10),(.70,.05,1.19),'METAL')
    # Octagonal fifth-wheel plate with a V slot, integrated as one body mesh.
    outline=[(-.53,-.40),(.53,-.40),(.66,-.22),(.66,.24),(.43,.43),
             (.11,.43),(.04,.02),(-.04,.02),(-.11,.43),(-.43,.43),(-.66,.24),(-.66,-.22)]
    vs=[(x,-z-1.9,y) for y in (1.19,1.28) for x,z in outline]
    n=len(outline)
    fs=[tuple(reversed(range(n))),tuple(range(n,2*n))]
    fs += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    b.add_mesh('SlottedFifthWheel',vs,fs,'DARK')
    b.box('FrontBumper',(-1.17,3.15,.38),(1.17,3.30,.65),'METAL')
    b.box('RearBumper',(-1.12,-3.30,.43),(1.12,-3.13,.61),'DARK')
    for s in (-1,1):
        lo,hi=sorted((s*.74,s*1.04))
        # Silver fuel tank between the wheels; cylinders run fore-aft.
        b.cylinder('FuelTank',(s*.82,-.25,.71),.27,1.12,10,'METAL')
        b.box('UpperEntryStep',(lo,.28,.90),(hi,1.10,1.08),'METAL')
        b.box('LowerEntryStep',(lo,.48,.53),(hi,1.02,.67),'METAL')
        lo,hi=sorted((s*1.03,s*1.25))
        b.box('MirrorFrame',(lo,.63,2.20),(hi,.84,2.71),'DARK')
        lo,hi=sorted((s*1.08,s*1.21))
        b.box('MirrorGlass',(lo,.61,2.24),(hi,.63,2.67),'METAL')
        # Exhausts run vertically; loft gives controlled six-sided facets.
        ring=[(s*.99+math.cos(i*math.pi/3)*.09,math.sin(i*math.pi/3)*.09) for i in range(6)]
        vs=[(x,.14+dy,h) for h in (1.32,3.21) for x,dy in ring]
        fs=[tuple(reversed(range(6))),tuple(range(6,12))]+[(i,(i+1)%6,(i+1)%6+6,i+6) for i in range(6)]
        b.add_mesh('ExhaustStack',vs,fs,'METAL')
        b.box('Headlamp',tuple([min(s*.84,s*1.10),2.89,1.24]),tuple([max(s*.84,s*1.10),3.03,1.47]),'CREAM')
        b.box('RearLamp',(min(s*.85,s*1.04),-3.31,.66),(max(s*.85,s*1.04),-3.28,.80),'RED')
    for x in (-.70,-.35,0,.35,.70):
        b.box('RoofMarker',(x-.065,.60,3.15),(x+.065,.75,3.25),'AMBER')


def trailer(b):
    # Chamfered box keeps long silhouette clean; ribs are in the atlas.
    box=b.loft('FreightBox',[(z,[(-1.25,1.40),(-1.25,3.68),(-1.13,3.80),
                  (1.13,3.80),(1.25,3.68),(1.25,1.40)]) for z in (-5.,5.)],'SIDE')
    assign_face_material(b,box,1,5.,'FRONT')
    assign_face_material(b,box,1,-5.,'REAR')
    box.data.materials.append(b.material('TOP'))
    for f in box.data.polygons:
        if min(box.data.vertices[i].co.z for i in f.vertices)>3.67:
            f.material_index=box.data.materials.find('TOP')
    for s in (-1,1):
        lo,hi=sorted((s*.42,s*.56))
        b.box('TrailerFrame',(lo,-4.78,1.07),(hi,4.8,1.40),'DARK')
        lo,hi=sorted((s*1.16,s*1.25))
        b.box('LowerSideRail',(lo,-5.,1.33),(hi,5.,1.43),'METAL')
        # Vertical corner extrusions; horizontal front and rear edge rails.
        for z in (-4.95,4.95):
            a,c=sorted((s*1.16,s*1.253))
            b.box('CornerPost',(a,z-.053,1.40),(c,z+.053,3.68),'METAL')
        lo,hi=sorted((s*.77,s*.93))
        b.box('LandingGearMount',(lo,2.26,1.28),(hi,2.54,1.42),'METAL')
        # Mudflaps behind rear axle: leave ample full tire diameter clearance.
        lo,hi=sorted((s*.87,s*1.23))
        b.box('RearMudflap',(lo,-4.88,.28),(hi,-4.81,.99),'DARK')
        for z in (-4.2,-3.):
            # Narrow suspension blocks remain between the left/right tires.
            b.box('Suspension',(min(s*.30,s*.58),z-.32,.68),(max(s*.30,s*.58),z+.32,1.14),'DARK')
        for z in (-4.82,-1.50,1.50,4.82):
            lo,hi=sorted((s*1.25,s*1.255))
            b.box('SideMarker',(lo,z-.08,1.49),(hi,z+.08,1.59),'AMBER')
    # Shallow concave door seam/lock bars are proper geometry at cargo end.
    for x in (-.68,.68):
        b.box('DoorLockBar',(x-.035,-5.018,1.58),(x+.035,-5.0,3.53),'METAL')
        for h in (1.74,2.56,3.40):
            b.box('BarBracket',(x-.075,-5.025,h),(x+.075,-5.,h+.055),'DARK')
    for x in (-1.16,1.16):
        for h in (1.63,2.26,2.94,3.51):
            b.box('DoorHinge',(x-.085,-5.025,h),(x+.085,-5.,h+.085),'METAL')
    b.box('UnderrideBar',(-1.15,-5.02,.44),(1.15,-4.84,.60),'METAL')
    for x in (-.73,.73):
        b.box('UnderrideSupport',(x-.065,-4.90,.54),(x+.065,-4.76,1.36),'DARK')
        b.box('RearStopLamp',(x-.18,-5.03,1.13),(x+.18,-4.97,1.27),'RED')
    # Kingpin is visible below the trailer nose. It meets tractor's fifth wheel.
    b.box('KingpinPlate',(-.42,3.55,1.32),(.42,4.45,1.4),'METAL')
    b.box('Kingpin',(-.07,3.93,1.28),(.07,4.07,1.34),'DARK')
    for z in (-4.9,4.9):
        b.box('RoofEndRail',(-1.14,z-.09,3.74),(1.14,z+.09,3.805),'METAL')


def map_uvs(obj,spec):
    mesh=obj.data
    uv=mesh.uv_layers.new(name='UVMap')
    for f in mesh.polygons:
        name=mesh.materials[f.material_index].name
        x0,y0,x1,y1=spec.REGIONS[name]
        pts=[mesh.vertices[mesh.loops[i].vertex_index].co for i in f.loop_indices]
        if name in spec.BOUNDS:
            axes=(1,2) if name=='SIDE' else ((0,1) if name=='TOP' else (0,2))
            bounds=spec.BOUNDS[name]
        else:
            n=f.normal
            axes=(0,1) if abs(n.z)>max(abs(n.x),abs(n.y)) else ((0,2) if abs(n.y)>abs(n.x) else (1,2))
            bounds=[(min(p[a] for p in pts),max(p[a] for p in pts)) for a in axes]
        for loop,p in zip(f.loop_indices,pts):
            uvv=[max(0,min(1,(p[a]-lo)/(hi-lo))) if hi-lo>1e-7 else .5 for a,(lo,hi) in zip(axes,bounds)]
            uv.data[loop].uv=((x0+2+uvv[0]*(x1-x0-4))/256,1-(y1-2-uvv[1]*(y1-y0-4))/256)


def main(slug=None):
    p=argparse.ArgumentParser()
    p.add_argument('--slug',default=slug,choices=['harrow_hauler','harrow_freight_trailer'])
    p.add_argument('--mesh',type=Path,required=True)
    p.add_argument('--blend',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    spec=importlib.import_module(args.slug+'_spec')
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    b=VehicleBuilder()
    (tractor if args.slug=='harrow_hauler' else trailer)(b)
    names=[o.name for o in b.objects]
    assert not any('wheel' in n.lower() and n!='SlottedFifthWheel' for n in names)
    for obj in b.objects: map_uvs(obj,spec)
    body=b.join();body.data.name=args.slug+'Body'
    for s in (-1,1):
        for axle in (spec.WHEELS['front_z'],spec.WHEELS['rear_z']):
            e=bpy.data.objects.new(f'WHEEL_{s}_{axle}',None)
            e.location=(s*spec.WHEELS['x'],axle,spec.WHEELS['arch_y'])
            bpy.context.collection.objects.link(e)
    e=bpy.data.objects.new('COUPLING',None)
    e.location=(spec.COUPLING['x'],spec.COUPLING['z'],spec.COUPLING['y'])
    bpy.context.collection.objects.link(e)
    body['source_components']=','.join(names)
    assert len([o for o in bpy.context.scene.objects if o.type=='MESH'])==1
    texture=Path(__file__).resolve().parents[1]/f'assets/textures/vehicles/{args.slug}/body.png'
    atlas=bpy.data.images.load(str(texture),check_existing=True)
    args.blend.parent.mkdir(parents=True,exist_ok=True)
    atlas.filepath=bpy.path.relpath(str(texture),start=str(args.blend.parent))
    for slot in body.material_slots:
        mat=slot.material;mat.use_nodes=True
        shader=mat.node_tree.nodes.get('Principled BSDF');shader.inputs['Roughness'].default_value=1
        node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas;node.interpolation='Closest'
        mat.node_tree.links.new(node.outputs['Color'],shader.inputs['Base Color'])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print(args.slug,export_emesh(body,args.mesh,args.slug))

if __name__=='__main__': main()
