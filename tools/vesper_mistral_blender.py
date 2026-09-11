#!/usr/bin/env python3
"""Mistral: explicit side pockets, continuous crown/hood and open cockpit."""
import argparse
import math
import sys
from pathlib import Path
import bpy
import bmesh
from mathutils import Vector

sys.path.insert(0,str(Path(__file__).resolve().parent))
from vesper_mistral_spec import ATLAS_SIZE,REGIONS,WHEELS,SHAPE,DRIVER,DOOR,TOP
from harrow_workman_blender import PickupBuilder as VehicleBuilder
from vesper_vx91_blender import export_emesh


def profile(y):
    """Own roadster plan taper and fender crown, not a borrowed silhouette."""
    keys=[(-2.35,.89,.74),(-2.07,1.00,.87),(-1.30,1.05,.96),
          (-.93,1.02,.94),(.50,1.01,.94),(1.48,1.05,.94),
          (1.98,1.00,.82),(2.35,.87,.69)]
    for a,b in zip(keys,keys[1:]):
        if a[0]-1e-6<=y<=b[0]+1e-6:
            t=(y-a[0])/(b[0]-a[0])
            return tuple(a[i]+t*(b[i]-a[i]) for i in (1,2))
    raise ValueError(y)


def stations():
    samples=[(-2.35,.22),(-2.07,.22),(-.93,.22),(.50,.22),(1.98,.22),(2.35,.22)]
    radius=SHAPE['arch_long_radius']; height=SHAPE['arch_height_radius']
    for axle in (WHEELS['rear_z'],WHEELS['front_z']):
        samples += [(axle-radius,.22),(axle-radius,.38)]
        samples += [(axle-radius*math.cos(i*math.pi/8),.38+height*math.sin(i*math.pi/8)) for i in range(1,8)]
        samples += [(axle+radius,.38),(axle+radius,.22)]
    # Keep styling stations out of the opening itself; arch stations already
    # interpolate the same crown profile and preserve the silhouette.
    return sorted((y,h) for y,h in samples if h!=.22 or not any(abs(y-a)<radius-1e-7 for a in (-1.30,1.48)))


def map_uvs(obj):
    mesh=obj.data
    uv=mesh.uv_layers.new(name='UVMap')
    for face in mesh.polygons:
        name=mesh.materials[face.material_index].name
        x0,y0,x1,y1=REGIONS[name]
        n=face.normal
        axes=(0,1) if abs(n.z)>=max(abs(n.x),abs(n.y)) else ((0,2) if abs(n.y)>abs(n.x) else (1,2))
        points=[mesh.vertices[mesh.loops[i].vertex_index].co for i in face.loop_indices]
        if name=='SIDE': axes=(1,2); bounds=((-2.35,2.35),(.20,1.0))
        elif name=='PAINT': axes=(0,1); bounds=((-1.05,1.05),(-2.35,2.35))
        elif name in ('FRONT','REAR'): axes=(0,2); bounds=((-1.05,1.05),(.20,.98))
        elif name=='GLASS': axes=(0,2); bounds=((- .78,.78),(.94,1.45))
        elif name=='TOP': axes=(0,1); bounds=((- .80,.80),(-.95,.10))
        else: bounds=tuple((min(p[a] for p in points),max(p[a] for p in points)) for a in axes)
        for loop,p in zip(face.loop_indices,points):
            ab=[(p[a]-lo)/(hi-lo) if hi-lo>1e-7 else .5 for a,(lo,hi) in zip(axes,bounds)]
            uv.data[loop].uv=((x0+2+ab[0]*(x1-x0-4))/256,1-(y1-2-ab[1]*(y1-y0-4))/256)


def beam(b,name,a,c,width,material):
    direction=(Vector(c)-Vector(a)).normalized()
    u=direction.cross(Vector((0,1,0))).normalized()*width*.5
    v=direction.cross(u).normalized()*width*.5
    pts=[tuple(Vector(p)+su*u+sv*v) for p in (a,c) for su,sv in ((-1,-1),(1,-1),(1,1),(-1,1))]
    return b.add_mesh(name,pts,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],material)


def clipped_ring(ring, plane, above, axis=1):
    """Clip a convex cross-section, retaining the exact original outer skin."""
    result=[]
    for a,c in zip(ring,ring[1:]+ring[:1]):
        inside_a=(a[axis]>=plane) if above else (a[axis]<=plane)
        inside_c=(c[axis]>=plane) if above else (c[axis]<=plane)
        if inside_a: result.append(a)
        if inside_a!=inside_c:
            t=(plane-a[axis])/(c[axis]-a[axis])
            result.append(tuple(a[i]+t*(c[i]-a[i]) for i in range(2)))
    return result


def split_driver_side(b, sections):
    rear,front,sill=DOOR['rear_z'],DOOR['front_z'],DOOR['sill_y']
    def at(y):
        for (ay,a),(cy,c) in zip(sections,sections[1:]):
            if ay<=y<=cy and cy>ay:
                t=(y-ay)/(cy-ay)
                return (y,[(ax+t*(cx-ax),az+t*(cz-az)) for (ax,az),(cx,cz) in zip(a,c)])
        raise ValueError(y)
    middle=[at(rear)]+[s for s in sections if rear<s[0]<front]+[at(front)]
    fixed=[b.loft('DriverRearQuarter',[s for s in sections if s[0]<rear]+[at(rear)],'SIDE'),
           b.loft('DriverFrontQuarter',[at(front)]+[s for s in sections if s[0]>front],'SIDE'),
           b.loft('DriverFixedSill',[(y,clipped_ring(r,sill,False)) for y,r in middle],'SIDE')]
    # The stationary loft has a deep structural wedge into the chassis. A
    # moving door ends at the inner trim instead; do not leave that wedge in
    # the doorway or carry it outward with the panel.
    door=b.loft('DriverDoorOuter',[(y,clipped_ring(clipped_ring(r,sill,True),
        DOOR['inner_x'],True,axis=0)) for y,r in middle],'SIDE')
    return fixed,door


def top_sections():
    """Canvas cross-sections front to back, each a closed (x, up) ring.

    The rail height drops faster than the crown does, so the raised top leaves
    the wedge-shaped side opening a roadster with its windows down should have.
    """
    out=[]
    for fwd,half_width,edge,centre in TOP['stations']:
        shoulder=edge+.78*(centre-edge)
        crown=[(-half_width,edge),(-.60*half_width,shoulder),(0.,centre),
               (.60*half_width,shoulder),(half_width,edge)]
        out.append((fwd,crown+[(x,z-TOP['thickness']) for x,z in reversed(crown)]))
    return out


def build_soft_top():
    """Two canvas bows. They share the joint station, so the fold has a seam."""
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    for material in list(bpy.data.materials): bpy.data.materials.remove(material)
    b=VehicleBuilder()
    sections=top_sections(); joint=TOP['joint']
    front=b.loft('SoftTopFront',sections[:joint+1],'TOP')
    rear=b.loft('SoftTopRear',sections[joint:],'TOP')
    for obj,name in ((front,'VesperMistralTopFront'),(rear,'VesperMistralTopRear')):
        bm=bmesh.new(); bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7)
        bm.to_mesh(obj.data); bm.free(); obj.data.update()
        map_uvs(obj); obj.data.name=name
    assert len([o for o in bpy.context.scene.objects if o.type=='MESH'])==2
    return front,rear


def build_vehicle(articulated=False):
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    # Both variants are cooked in a factory-only Blender process. Reset names
    # so the second build uses exactly the same semantic atlas materials.
    for material in list(bpy.data.materials): bpy.data.materials.remove(material)
    b=VehicleBuilder()
    door_parts=[]
    samples=stations()
    for side in (-1,1):
        sections=[]
        for y,lower in samples:
            w,h=profile(y)
            ring=[(.50,lower),(w-.045,lower),(w,lower+.025),
                  (w,h-.06),(.90*w,h),(.78,h)]
            sections.append((y,[(side*x,z) for x,z in ring]))
        if articulated and side==1:
            fixed,door=split_driver_side(b,sections)
            side_objects=fixed+[door]; door_parts.append(door)
        else: side_objects=[b.loft('OpenPocketSide'+str(side),sections,'SIDE')]
        for obj in side_objects:
            obj.data.materials.append(b.material('PAINT'))
            obj.data.materials.append(b.material('SHADOW'))
            if articulated: obj.data.materials.append(b.material('INTERIOR'))
            for face in obj.data.polygons:
                if face.normal.z<-.25: face.material_index=2
                elif face.normal.z>.6: face.material_index=1
                elif articulated and side==1 and (abs(face.normal.y)>.99 or face.normal.x<-.5): face.material_index=3
    b.box('NarrowSpine',(-.43,-2.25,.20),(.43,2.25,.34),'SHADOW')
    # Hood/deck share exact top station edges with the broad side crowns.
    # Only the cockpit span is omitted. There is no full-width axle tunnel.
    ys=sorted(set([y for y,_ in samples]+[-.93,.50]))
    for label,lo,hi in (('Hood',.50,2.35),('RearDeck',-2.35,-.93)):
        sect=[]
        for y in ys:
            if lo-1e-6<=y<=hi+1e-6:
                h=profile(y)[1]
                sect.append((y,[(-.78,h-.055),(-.78,h),(-.38,h-.025),
                                (.38,h-.025),(.78,h),(.78,h-.055)]))
        b.loft(label,sect,'PAINT')
    # Real floor, inner door walls, bulkhead, dash and two separate seat forms.
    b.box('CockpitFloor',(-.78,-.93,.34),(.78,.50,.40),'INTERIOR')
    b.box('RearBulkhead',(-.78,-.96,.40),(.78,-.86,.92),'INTERIOR')
    b.box('Dashboard',(-.78,.31,.70),(.78,.50,.93),'DASH')
    b.box('CentreConsole',(-.10,-.84,.40),(.10,.32,.59),'RUBBER')
    for side in (-1,1):
        x=side*.43
        b.box('SeatCushion'+str(side),(x-.245,-.69,.43),(x+.245,-.10,.59),'SEAT')
        b.loft('SeatBack'+str(side),[
            (-.80,[(x-.24,.50),(x-.24,1.05),(x-.17,1.10),(x+.17,1.10),(x+.24,1.05),(x+.24,.50)]),
            (-.63,[(x-.24,.50),(x-.24,1.01),(x-.17,1.06),(x+.17,1.06),(x+.24,1.01),(x+.24,.50)])],'SEAT')
        lo,hi=sorted((side*.74,side*.78))
        # The clipped articulated shell supplies its own capped inner panel;
        # a second box here would overlap its new x=.74 interior surface.
        if not (articulated and side==1):
            b.box('DoorInterior'+str(side),(lo,-.86,.44),(hi,.31,.87),'INTERIOR')
        beam(b,'WindshieldPost'+str(side),(side*.78,.50,.94),(side*.69,.05,1.42),.055,'PAINT')
        lo,hi=sorted((side*.98,side*1.045))
        b.box('Mirror'+str(side),(lo,.36,.91),(hi,.56,1.01),'RUBBER')
    beam(b,'WindshieldHeader',(-.69,.05,1.42),(.69,.05,1.42),.055,'PAINT')
    # Closed thin glazing prism gives correct culling on both faces.
    a=[(-.756,.489,.95),(.756,.489,.95),(.675,.061,1.401),(-.675,.061,1.401)]
    verts=a+[(x,y-.012,z) for x,y,z in a]
    b.add_mesh('Windshield',verts,[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],'GLASS')
    # A hollow octagonal steering rim, plus two spokes. Interior belongs to BODY.
    verts=[]
    for y in (.12,.15):
        for radius in (.13,.098):
            verts += [(DRIVER['left_x']+radius*math.cos(i*math.tau/8),y,DRIVER['wheel_y']+radius*math.sin(i*math.tau/8)) for i in range(8)]
    faces=[]
    for i in range(8):
        j=(i+1)%8
        faces += [(i,j,j+8,i+8),(i+16,i+24,j+24,j+16),(i,j,j+16,i+16),(i+8,i+24,j+24,j+8)]
    b.add_mesh('SteeringRim',verts,faces,'BLACK')
    beam(b,'SteeringSpoke',(.31,.135,1.0),(.55,.135,1.0),.028,'METAL')
    beam(b,'SteeringColumn',(.43,.33,.89),(.43,.135,1.0),.045,'BLACK')
    for x in DRIVER['pedals_x']:
        b.box('DriverPedal'+str(x),(x-.042,.38,.405),(x+.042,.46,.43),'METAL')
    b.box('GearLever',(-.022,-.02,.58),(.022,.024,.74),'BLACK')
    # End faces are physical shell receivers. Flush lights are painted directly
    # into them, so no floating cards or fleet-specific surface bake is needed.
    for y in (-2.35,2.35):
        h=profile(y)[1]
        b.box('EndCentre'+str(y),(-.78,y-(.035 if y>0 else 0),.22),(.78,y+(.035 if y<0 else 0),h-.055),'SIDE')
    for obj in b.objects:
        for f in obj.data.polygons:
            coords=[obj.data.vertices[i].co for i in f.vertices]
            if all(abs(p.y-2.35)<1e-5 for p in coords): name='FRONT'
            elif all(abs(p.y+2.35)<1e-5 for p in coords): name='REAR'
            else: continue
            mat=b.material(name)
            if name not in obj.data.materials: obj.data.materials.append(mat)
            f.material_index=obj.data.materials.find(name)
        bm=bmesh.new(); bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6)
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7)
        bm.to_mesh(obj.data); bm.free(); obj.data.update()
        map_uvs(obj)
    if articulated:
        b.objects=[obj for obj in b.objects if obj not in door_parts]
        body=b.join(); body.name='BODY_OPEN'; body.data.name='VesperMistralOpenBody'
        b.objects=door_parts
        door=b.join(); door.name='DRIVER_DOOR'; door.data.name='VesperMistralDriverDoor'
    else:
        body=b.join(); body.data.name='VesperMistralBody'
    for side in (-1,1):
        for label,axle in (('F',1.48),('R',-1.30)):
            empty=bpy.data.objects.new('WHEEL_'+label+('L' if side<0 else 'R'),None)
            empty.location=(side*.91,axle,.38); bpy.context.collection.objects.link(empty)
    assert len([o for o in bpy.context.scene.objects if o.type=='MESH'])==(2 if articulated else 1)
    return (body,door) if articulated else body


if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--mesh',type=Path,required=True); p.add_argument('--blend',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    body=build_vehicle(); args.blend.parent.mkdir(parents=True,exist_ok=True)
    atlas=bpy.data.images.load(str(Path(__file__).resolve().parents[1]/'assets/textures/vehicles/vesper_mistral/body.png'))
    atlas.pack()
    for slot in body.material_slots:
        mat=slot.material; mat.use_nodes=True
        tex=mat.node_tree.nodes.new('ShaderNodeTexImage'); tex.image=atlas; tex.interpolation='Closest'
        shader=mat.node_tree.nodes.get('Principled BSDF'); shader.inputs['Roughness'].default_value=1
        mat.node_tree.links.new(tex.outputs['Color'],shader.inputs['Base Color'])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print('VESPER_MISTRAL',export_emesh(body,args.mesh,'vesper_mistral'))
    body_open,door=build_vehicle(articulated=True)
    print('VESPER_MISTRAL_OPEN',export_emesh(body_open,args.mesh.with_name('body_open.emesh'),'vesper_mistral'))
    print('VESPER_MISTRAL_DOOR',export_emesh(door,args.mesh.with_name('driver_door.emesh'),'vesper_mistral'))
    top_front,top_rear=build_soft_top()
    print('VESPER_MISTRAL_TOP_FRONT',export_emesh(top_front,args.mesh.with_name('soft_top_front.emesh'),'vesper_mistral'))
    print('VESPER_MISTRAL_TOP_REAR',export_emesh(top_rear,args.mesh.with_name('soft_top_rear.emesh'),'vesper_mistral'))
