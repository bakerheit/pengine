#!/usr/bin/env python3
"""Build the original 1991 Saddle Tango body from the selected B references."""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from saddle_tango_spec import MODEL, REGIONS, TEXTURE, WHEELS
from saddle_tango_surface import (FRONT, REAR, SILL, FRONT_DOOR_EDGE, B_PILLAR,
                                  FRONT_AXLE, REAR_AXLE, ARCH_CENTRE,
                                  ARCH_RADIUS, arch_bottom, cabin_x,
                                  rear_door_edge, side_x, top_at, width_at)
from vesper_vx91_blender import VehicleBuilder, export_emesh


def beam(builder, name, start, end, width, material):
    direction = (Vector(end) - Vector(start)).normalized()
    reference = Vector((0, 0, 1)) if abs(direction.z) < .9 else Vector((1, 0, 0))
    across = direction.cross(reference).normalized() * width * .5
    depth = direction.cross(across).normalized() * width * .5
    section = (across, -.5 * across + .866 * depth,
               -.5 * across - .866 * depth)
    vertices = [tuple(Vector(point) + offset)
                for point in (start, end) for offset in section]
    return builder.add_mesh(name, vertices,
                            [(0, 2, 1), (3, 4, 5), (0, 1, 4, 3),
                             (1, 2, 5, 4), (2, 0, 3, 5)], material)


def grid(builder, name, rows, material, smooth=False, inward=False):
    """A sampled stamping with quads and optional smooth vertex normals."""
    width = len(rows[0])
    vertices = [vertex for row in rows for vertex in row]
    faces = [(i*width+j, (i+1)*width+j,
              (i+1)*width+j+1, i*width+j+1)
             for i in range(len(rows)-1) for j in range(width-1)]
    if inward:
        faces=[tuple(reversed(face)) for face in faces]
    obj = builder.add_mesh(name, vertices, faces, material)
    if smooth:
        for face in obj.data.polygons:
            face.use_smooth = True
    return obj


def curve_beam(builder, name, points, width, material):
    for i in range(1, len(points)):
        beam(builder, name+str(i), points[i-1], points[i], width, material)


def outer_panel(builder, name, side, back, front, steps):
    """Door and fender skins share side_x and the same wheel-hole boundary."""
    rows = []
    for vi in range(17):
        v = vi/16
        a = back(v) if callable(back) else back
        b = front(v) if callable(front) else front
        row = []
        for qi in range(steps+1):
            q = a+(b-a)*qi/steps
            low = arch_bottom(q)
            z = low+(top_at(q)-low)*v
            row.append((side*side_x(q,z), q, z))
        rows.append(row)
    obj = grid(builder, name+'PressedOuterSkin', rows, 'BODY_SIDE', True)
    # Backside material and normals matter once the windows are transparent.
    inner = [[(x-side*.045, y, z) for x,y,z in row] for row in rows]
    grid(builder,name+'InwardSkin',inner,
         'DOOR_CARD' if 'Door' in name else 'BODY_SIDE',True,True)
    if 'Door' in name:
        # Real skin depth and aperture edge returns, as on the Constant.
        for label,edge in (('Rear',0),('Front',-1)):
            grid(builder,name+label+'EdgeReturn',
                 [[rows[i][edge],inner[i][edge]] for i in range(len(rows))],
                 'BODY_SIDE')
    return obj


def add_body(builder):
    for side in (-1,1):
        outer_panel(builder,'RearQuarter'+str(side),side,REAR,rear_door_edge,34)
        outer_panel(builder,'RearDoor'+str(side),side,rear_door_edge,B_PILLAR,30)
        outer_panel(builder,'FrontDoor'+str(side),side,B_PILLAR,
                    FRONT_DOOR_EDGE,30)
        outer_panel(builder,'FrontFender'+str(side),side,
                    FRONT_DOOR_EDGE,FRONT,39)
        for axle,label in ((REAR_AXLE,'Rear'),(FRONT_AXLE,'Front')):
            # The arch is a cutout in the stamping, with a dark return into
            # the wheel well. It is not a disk pasted over a solid side wall.
            rows=[]
            for i in range(33):
                q = axle-ARCH_RADIUS+2*ARCH_RADIUS*i/32
                z = arch_bottom(q)
                rows.append([(side*side_x(q,z),q,z),
                             (side*(side_x(q,z)-.065),q,z-.005)])
            grid(builder,label+'ArchReturn'+str(side),rows,'BODY_SIDE')
    # Crown and taper are continuous from the A-pillars to the front lamps.
    for name,a,b,count in (('LongHood',FRONT_DOOR_EDGE,FRONT,42),
                           ('NotchbackDeck',REAR,-1.65,30)):
        rows=[]
        for i in range(count+1):
            q=a+(b-a)*i/count
            edge=side_x(q,top_at(q))
            rows.append([(edge*u,q,top_at(q)+.027*(1-u*u))
                         for u in (-1,-.92,-.78,-.60,-.30,0,.30,.60,.78,.92,1)])
        grid(builder,name+'CrownedStamping',rows,'BODY_TOP',True)
        for side in (-1,1):
            curve_beam(builder,name+'ShoulderGap'+str(side),
                       [(side*side_x(a+(b-a)*i/count,
                                     top_at(a+(b-a)*i/count)),
                         a+(b-a)*i/count,top_at(a+(b-a)*i/count)+.001)
                        for i in range(count+1)],.0035,'SEAM')
    builder.box('CentralChassis',(-.44,REAR+.08,.19),
                (.44,FRONT-.08,.29),'DARK')


def screen(builder,name,front):
    base_q,roof_q=(.57,.07) if front else (-1.65,-1.26)
    rows=[]
    for i in range(13):
        t=i/12
        z=1.005+(.438*t)
        q=base_q+(roof_q-base_q)*t
        half=cabin_x(z)-.025
        rows.append([(u*half,q+.019*(1-u*u)*(1 if front else -1),
                      z+.012*(1-u*u))
                     for u in (-1,-.9,-.75,-.5,-.25,0,.25,.5,.75,.9,1)])
    grid(builder,name+'InSetGlass',rows,'GLASS',True)
    for side in (-1,1):
        curve_beam(builder,name+'SideSeal'+str(side),
                   [row[-1 if side>0 else 0] for row in rows],.031,'RUBBER')
    for row,label in ((rows[0],'Base'),(rows[-1],'Top')):
        curve_beam(builder,name+label+'Seal',row,.025,'RUBBER')


def side_window(builder,name,side,outline):
    """Inset glass, painted frame, and rubber seal on the cabin contour."""
    centre_q=sum(p[0] for p in outline)/len(outline)
    centre_z=sum(p[1] for p in outline)/len(outline)
    inner=[(centre_q+(q-centre_q)*.94,
            centre_z+(z-centre_z)*.89) for q,z in outline]
    for label,loop,width,material in (('PaintedFrame',outline,.036,'BODY_SIDE'),
                                       ('RubberSeal',inner,.017,'RUBBER')):
        mapped=[(side*cabin_x(z),q,z) for q,z in loop]
        for i in range(len(mapped)):
            beam(builder,name+label+str(i),mapped[i],
                 mapped[(i+1)%len(mapped)],width,material)
    glass=[(side*(cabin_x(z)-.009),q,z) for q,z in inner]
    builder.panel(name+'InSetGlass',glass,'GLASS',(side,0,0))


def cabin_box(builder,name,side,q0,q1,z0,z1,inner_x,outer_x,material):
    x0,x1=sorted((side*inner_x,side*outer_x))
    return builder.box(name,(x0,q0,z0),(x1,q1,z1),material)


def door_interiors(builder):
    for side in (-1,1):
        for label,q0,q1 in (('Front',-.40,.61),('Rear',-1.24,-.40)):
            # Cloth insert follows the inner door contour. The back of the
            # door has its own inward normal and a continuous vinyl texture.
            pad=.12
            corners=[(q0+pad,.64),(q1-pad,.64),
                     (q1-pad,.81),(q0+pad,.81)]
            builder.panel(label+'DoorClothInset'+str(side),
                          [(side*(side_x(q,z)-.057),q,z) for q,z in corners],
                          'SEAT_FABRIC',(-side,0,0))
            cabin_box(builder,label+'DoorArmrest'+str(side),side,
                      q0+.11,q1-.11,.55,.615,.73,.855,'DOOR_CARD')
            cabin_box(builder,label+'DoorPull'+str(side),side,
                      q1-.24,q1-.07,.69,.73,.745,.825,'DASHBOARD')
            cabin_box(builder,label+'InteriorLatch'+str(side),side,
                      q1-.24,q1-.13,.83,.875,.82,.86,'METAL')
            cabin_box(builder,label+'DoorPocket'+str(side),side,
                      q0+.10,q1-.10,.35,.42,.77,.87,'DASHBOARD')
        cabin_box(builder,'RearQuarterInside'+str(side),side,
                  -1.72,-1.28,.43,.97,.77,.835,'DOOR_CARD')
        # Close the inboard face of the rear wheelhouse. Without this, the
        # wheel and bright street show through the rear passenger door.
        vertices=[]
        for i in range(25):
            q=-1.96+i*1.20/24
            arch_t=(q-REAR_AXLE)/.60
            top=max(.55,arch_bottom(q)+.16,
                    .55+.41*max(0.0,1-arch_t*arch_t))
            vertices.extend(((side*.70,q,.26),(side*.70,q,top),
                             (side*.76,q,.26),(side*.76,q,top)))
        faces=[]
        for i in range(24):
            a=i*4;b=(i+1)*4
            faces.extend(((a,b,b+1,a+1),(a+2,a+3,b+3,b+2),
                          (a+1,b+1,b+3,a+3),(a,a+2,b+2,b)))
        faces.extend(((0,1,3,2),(96,98,99,97)))
        builder.add_mesh('RearWheelhouseLiner'+str(side),vertices,faces,'CARPET')


def steering_wheel(builder):
    cx,q,cz=.38,.255,.84
    count=24
    vertices=[]
    for radius in (.117,.147):
        vertices.extend((cx+radius*math.cos(i*math.tau/count),
                         q,cz+radius*math.sin(i*math.tau/count))
                        for i in range(count))
    faces=[(i,(i+1)%count,count+(i+1)%count,count+i)
           for i in range(count)]
    builder.add_mesh('DriverSteeringRim',vertices,faces,'DASHBOARD')
    for i,angle in enumerate((math.pi/2,math.pi*7/6,math.pi*11/6)):
        beam(builder,'SteeringSpoke'+str(i),(cx,q,cz),
             (cx+.125*math.cos(angle),q,cz+.125*math.sin(angle)),
             .018,'METAL')
    builder.box('SteeringHub',(.34,.24,.80),(.42,.265,.88),'DASHBOARD')


def add_cabin(builder):
    rows=[]
    for i in range(22):
        t=i/21
        q=-1.26+1.33*t
        rows.append([(u*.655,q+.017*(1-u*u)*math.sin(math.pi*t),
                      1.443+.027*(1-u*u))
                     for u in (-1,-.9,-.75,-.5,-.25,0,.25,.5,.75,.9,1)])
    grid(builder,'ContinuousCrownedRoofSkin',rows,'BODY_TOP',True)
    grid(builder,'TexturedHeadliner',
         [[(x,q,z-.031) for x,q,z in row] for row in rows],
         'HEADLINER',True,True)
    screen(builder,'Windshield',True)
    screen(builder,'RearWindow',False)
    for side in (-1,1):
        side_window(builder,'FrontDoorWindow'+str(side),side,
                    [(.53,1.015),(-.37,1.015),(-.38,1.43),(.045,1.43)])
        side_window(builder,'RearDoorWindow'+str(side),side,
                    [(-.43,1.015),(-1.15,1.015),(-1.12,1.43),(-.44,1.43)])
        side_window(builder,'FixedQuarterWindow'+str(side),side,
                    [(-1.19,1.015),(-1.54,1.015),(-1.25,1.43),
                     (-1.16,1.43)])
        for label,a,c in [('A',(.57,1.00),(.07,1.45)),
                          ('B',(-.405,1.00),(-.405,1.45)),
                          ('C',(-1.65,1.00),(-1.26,1.45))]:
            beam(builder,label+'Pillar'+str(side),
                 (side*cabin_x(a[1]),a[0],a[1]),
                 (side*cabin_x(c[1]),c[0],c[1]),
                 .082 if label=='C' else .052,
                 'BODY_SIDE' if label!='B' else 'RUBBER')
        curve_beam(builder,'RoofSideBrightwork'+str(side),
                   [(side*.657,-1.26+i*1.33/16,1.447)
                   for i in range(17)],.014,'METAL')
    door_interiors(builder)
    builder.box('CarpetedCabinFloor',(-.70,-1.63,.28),(.70,.53,.34),'CARPET')
    builder.box('FrontBulkhead',(-.70,.52,.29),(.70,.55,1.00),'DASHBOARD')
    builder.box('PaddedDashboard',(-.72,.36,.79),(.72,.52,.98),'DASHBOARD')
    builder.box('GaugeCluster',(.18,.344,.81),(.60,.367,.945),'DARK')
    for x in (.30,.47):
        builder.box('GaugeBezel'+str(x),(x-.05,.337,.845),
                    (x+.05,.346,.91),'SILVER_DARK')
    builder.box('CenterStack',(-.18,.333,.65),(.18,.365,.91),'DARK')
    for z in (.75,.80,.85):
        builder.box('CenterControl'+str(z),(-.12,.328,z),
                    (.12,.334,z+.012),'SILVER_DARK')
    builder.box('CenterConsole',(-.14,-.38,.34),(.14,.33,.50),'DASHBOARD')
    steering_wheel(builder)
    builder.box('RearviewMirror',(-.13,.05,1.29),(.13,.075,1.34),'DASHBOARD')
    for label,x in (('Driver',.39),('Passenger',-.39)):
        builder.box(label+'SeatBase',(x-.23,-.22,.37),(x+.23,.15,.52),'SEAT_FABRIC')
        builder.box(label+'SeatBack',(x-.22,-.33,.48),(x+.22,-.22,1.12),'SEAT_FABRIC')
        builder.box(label+'Headrest',(x-.13,-.32,1.09),(x+.13,-.24,1.27),'SEAT_FABRIC')
        for i,edge in enumerate((x-.235,x+.215)):
            builder.box(label+'SeatBolster'+str(i),
                        (edge,-.20,.48),(edge+.02,.15,.54),'DOOR_CARD')
    builder.box('RearBench',(-.70,-1.48,.37),(.70,-.75,.55),'SEAT_FABRIC')
    builder.box('RearBack',(-.70,-1.59,.50),(.70,-1.48,1.11),'SEAT_FABRIC')
    builder.box('RearParcelShelf',(-.71,-1.65,.94),(.71,-1.36,.97),'DASHBOARD')


def end_face_q(x,front):
    q=FRONT if front else REAR
    direction=1 if front else -1
    half=width_at(q)
    return q-direction*.047*abs(x/half)**3


def end_lens(builder,name,x0,x1,z0,z1,front,material):
    """Fit lamp glass onto the wrapped end stamping, including its corners."""
    rows=[]
    for i in range(4):
        z=z0+(z1-z0)*i/3
        xs=[x0+(x1-x0)*j/8 for j in range(9)]
        if not front:xs.reverse()
        rows.append([(x,end_face_q(x,front)+(.024 if front else -.024),z)
                     for x in xs])
    grid(builder,name,rows,material,True)


def wrapped_bumper(builder,front):
    label='Front' if front else 'Rear'
    direction=1 if front else -1
    xs=[-.87+i*1.74/24 for i in range(25)]
    def face_q(x):
        return (2.28 if front else -2.75)-direction*.09*abs(x/.87)**4
    rows=[[(x,face_q(x),z) for x in (xs if front else list(reversed(xs)))]
          for z in (.235,.38,.425)]
    grid(builder,label+'BumperWrappedFace',rows,'BODY_END')
    for z,name in ((.24,'LowerReturn'),(.425,'UpperReturn')):
        grid(builder,label+'Bumper'+name,
             [[(x,face_q(x),z),(x,
                 face_q(x)-direction*.06,z)] for x in xs],
             'BODY_END')
    curve_beam(builder,label+'BumperBrightwork',
               [(x,face_q(x)+direction*.007,.372) for x in xs],
               .012,'METAL')


def add_details(builder):
    # Broad, nearly upright 1991 lamp panel with curved wraparound corners.
    for name,q,base,top,side in (('Nose',FRONT,.25,top_at(FRONT),1),
                                  ('Tail',REAR,.26,top_at(REAR),-1)):
        rows=[]
        for i in range(13):
            z=base+(top-base)*i/12
            half=side_x(q,z)
            rows.append([(half*u,q-side*.047*abs(u)**3,
                          z+.027*(1-u*u)*(i/12)**8)
                         for u in (-1,-.9,-.75,-.5,-.25,0,.25,.5,.75,.9,1)])
        grid(builder,name+'WraparoundStamping',rows,'BODY_END',True)
    for side in (-1,1):
        inner,outer=sorted((side*.47,side*.775))
        amber_a,amber_b=sorted((side*.775,side*.855))
        end_lens(builder,'Headlamp'+str(side),inner,outer,.55,.75,
                 True,'HEADLIGHT')
        end_lens(builder,'FrontAmber'+str(side),amber_a,amber_b,.55,.75,
                 True,'AMBER')
        split=side*.625
        beam(builder,'HeadlampDivider'+str(side),
             (split,end_face_q(split,True)+.008,.55),
             (split,end_face_q(split,True)+.008,.75),.008,'RUBBER')
        tail_a,tail_b=sorted((side*.49,side*.84))
        end_lens(builder,'Taillamp'+str(side),tail_a,tail_b,.55,.79,
                 False,'TAIL')
        reverse_a,reverse_b=sorted((side*.51,side*.60))
        end_lens(builder,'ReverseCell'+str(side),reverse_a,reverse_b,.57,.65,
                 False,'REVERSE')
        marker=[(side*(side_x(q,z)+.003),q,z)
                for q,z in ((1.82,.58),(2.02,.58),(2.02,.66),(1.82,.66))]
        builder.panel('FrontMarker'+str(side),marker,'AMBER',(side,0,0))
        # Shut lines now follow the stamped skin instead of floating at X=.961.
        for label,seam in (('Front',lambda v:FRONT_DOOR_EDGE),
                           ('B',lambda v:B_PILLAR),
                           ('Rear',rear_door_edge)):
            points=[]
            for i in range(21):
                v=i/20
                q=seam(v)
                z=arch_bottom(q)+(top_at(q)-arch_bottom(q))*v
                points.append((side*(side_x(q,z)+.003),q,z))
            curve_beam(builder,label+'DoorShut'+str(side),points,
                       .007,'SEAM')
        for y in (.04,-.98):
            x=side_x(y,.87)
            builder.box('Handle'+str((side,y)),
                        (min(side*x,side*(x+.023)),y-.082,.86),
                        (max(side*x,side*(x+.023)),y+.082,.92),'SILVER_DARK')
        for label,z,width,material in (('SideMolding',.58,.018,'METAL'),
                                        ('MoldingShadow',.545,.022,'RUBBER'),
                                        ('LowerCladdingEdge',.285,.016,'RUBBER')):
            for a,b in ((-2.51,-1.83),(-.91,.59),(.60,.91),(1.83,2.08)):
                curve_beam(builder,label+str((side,a)),
                           [(side*(side_x(a+(b-a)*i/14,z)+.004),
                             a+(b-a)*i/14,z) for i in range(15)],
                           width,material)
        builder.box('Mirror'+str(side),
                    (min(side*.84,side*1.00),.45,1.00),
                    (max(side*.84,side*1.00),.67,1.13),'BODY_SIDE')
    builder.box('Grille',(-.46,2.203,.52),(.46,2.213,.755),'DARK')
    for z in (.55,.605,.66,.715):
        beam(builder,'GrilleSlat'+str(z),(-.45,2.216,z),
             (.45,2.216,z),.010,'SILVER_DARK')
    # A small raised S is enough to separate Saddle from Legacy's grille.
    for i,(a,c) in enumerate([
        ((-.035,2.224,.683),(.033,2.224,.683)),
        ((-.035,2.224,.683),(-.035,2.224,.64)),
        ((-.035,2.224,.64),(.035,2.224,.64)),
        ((.035,2.224,.64),(.035,2.224,.60)),
        ((.035,2.224,.60),(-.035,2.224,.60))]):
        beam(builder,'SBadge'+str(i),a,c,.011,'METAL')
    wrapped_bumper(builder,True)
    wrapped_bumper(builder,False)
    builder.box('LowerIntake',(-.61,2.286,.285),(.61,2.297,.36),'DARK')
    for x in (-.70,.70):
        builder.box('FogLamp'+str(x),(x-.075,2.285,.285),
                    (x+.075,2.301,.36),'HEADLIGHT')
    builder.box('RearPlateRecess',(-.26,-2.704,.52),
                (.26,-2.67,.72),'DARK')
    builder.box('DeckLip',(-.72,-2.60,.913),(.72,-2.48,.944),'BODY_TOP')


def assign_uvs(obj):
    mesh=obj.data
    uv=mesh.uv_layers.new(name='SaddleAtlas')
    for face in mesh.polygons:
        name=mesh.materials[face.material_index].name
        x0,y0,x1,y1=REGIONS[name]
        for li in face.loop_indices:
            p=mesh.vertices[mesh.loops[li].vertex_index].co
            if name=='BODY_SIDE':
                u=max(0,min(1,(p.y+2.75)/5.5));v=max(0,min(1,p.z/1.1))
            elif name=='BODY_TOP':
                u=max(0,min(1,(p.x+1.)/2.));v=max(0,min(1,(p.y+2.75)/5.5))
            elif name=='DOOR_CARD':
                u=max(0,min(1,(p.y+1.8)/2.5));v=max(0,min(1,p.z/1.05))
            elif name in ('SEAT_FABRIC','HEADLINER','CARPET','DASHBOARD'):
                u=max(0,min(1,(p.x+1.)/2.))
                v=max(0,min(1,(p.y+1.7)/2.35))
            elif name in ('HEADLIGHT','AMBER','TAIL','REVERSE'):
                # The lamp checker and glow shader need actual lens triangles
                # to cover texels. A single UV point collapses them to zero.
                bounds={
                    'HEADLIGHT':(.47,.775,.285,.76),
                    'AMBER':(.775,.855,.55,.75),
                    'TAIL':(.49,.84,.55,.79),
                    'REVERSE':(.51,.60,.57,.65),
                }
                xa,xb,za,zb=bounds[name]
                u=max(0,min(1,(abs(p.x)-xa)/(xb-xa)))
                v=max(0,min(1,(p.z-za)/(zb-za)))
            else:
                u=max(0,min(1,(p.x+1.)/2.)) if name=='BODY_END' else .5
                v=max(0,min(1,p.z/1.1)) if name=='BODY_END' else .5
            uv.data[li].uv=((x0+2+u*(x1-x0-4))/256,
                            1-(y1-2-v*(y1-y0-4))/256)


def make_wheel():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    b=VehicleBuilder()
    segments=24
    sections=[(-.11,.25),(-.115,.30),(-.095,.35),(.095,.35),
              (.115,.30),(.11,.25)]
    vertices=[(x,r*math.cos(i*math.tau/segments),
                  r*math.sin(i*math.tau/segments))
              for x,r in sections for i in range(segments)]
    faces=[]
    for s in range(len(sections)):
        nxt=(s+1)%len(sections)
        for i in range(segments):
            j=(i+1)%segments
            faces.append((s*segments+i,s*segments+j,
                          nxt*segments+j,nxt*segments+i))
    b.add_mesh('PeriodTire',vertices,faces,'RUBBER')
    for side in (-1,1):
        x=side*.118
        ring=[]
        for radius in (.245,.215):
            ring += [(x,radius*math.cos(i*math.tau/segments),
                        radius*math.sin(i*math.tau/segments))
                     for i in range(segments)]
        faces=[]
        for i in range(segments):
            j=(i+1)%segments
            faces.append((i,j,segments+j,segments+i))
        b.add_mesh('MachinedRim'+str(side),ring,faces,'METAL')
        # Two crossed spoke sets read as a 1991 basketweave wheel in motion.
        for i in range(12):
            a=i*math.tau/12
            for offset in (-.23,.23):
                start=(x,.075*math.cos(a),.075*math.sin(a))
                end=(x,.214*math.cos(a+offset),.214*math.sin(a+offset))
                beam(b,'BasketSpoke'+str((side,i,offset)),start,end,
                     .012,'METAL')
        hub=[(x+side*.003,.058*math.cos(i*math.tau/12),
              .058*math.sin(i*math.tau/12)) for i in range(12)]
        b.add_mesh('CentreCap'+str(side),hub,[tuple(range(12))],
                   'SILVER_DARK')
    for obj in b.objects:assign_uvs(obj)
    wheel=b.join();wheel.name='WHEEL';wheel.data.name='SaddleBasketweaveWheel'
    print('SADDLE_TANGO_WHEEL',export_emesh(wheel,MODEL/'front_wheel.emesh',
                                            'saddle_tango_wheel'))
    export_emesh(wheel,MODEL/'rear_wheel.emesh','saddle_tango_wheel')
    atlas=bpy.data.images.load(str(TEXTURE),check_existing=True)
    for slot in wheel.material_slots:
        mat=slot.material;mat.use_nodes=True
        node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas
        node.interpolation='Closest'
        shader=mat.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Roughness'].default_value=.9
        mat.node_tree.links.new(node.outputs['Color'],shader.inputs['Base Color'])
    bpy.ops.wm.save_as_mainfile(filepath=str(MODEL/'wheel_source.blend'))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--mesh',type=Path,default=MODEL/'body.emesh')
    parser.add_argument('--blend',type=Path,default=MODEL/'source.blend')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    bpy.ops.wm.read_factory_settings(use_empty=True)
    b=VehicleBuilder()
    add_body(b)
    add_cabin(b)
    add_details(b)
    glass_names={
        'WindshieldInSetGlass':'windshield',
        'RearWindowInSetGlass':'rear_glass',
        'FrontDoorWindow1InSetGlass':'driver_glass',
        'FrontDoorWindow-1InSetGlass':'passenger_glass',
        'RearDoorWindow1InSetGlass':'driver_rear_glass',
        'FixedQuarterWindow1InSetGlass':'driver_rear_glass',
        'RearDoorWindow-1InSetGlass':'passenger_rear_glass',
        'FixedQuarterWindow-1InSetGlass':'passenger_rear_glass',
    }
    glass_groups={name:[] for name in (
        'windshield','rear_glass','passenger_glass','driver_glass',
        'driver_rear_glass','passenger_rear_glass')}
    for obj in b.objects:
        if obj.name in glass_names:
            glass_groups[glass_names[obj.name]].append(obj)
    assert sum(len(group) for group in glass_groups.values())==8
    for obj in b.objects:
        bm=bmesh.new();bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-7)
        bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-8)
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        bm.to_mesh(obj.data);bm.free();obj.data.update()
        assign_uvs(obj)
    b.objects=[obj for obj in b.objects if obj.name not in glass_names]
    body=b.join();body.name='BODY';body.data.name='SaddleTangoBody'
    bm=bmesh.new();bm.from_mesh(body.data)
    bmesh.ops.triangulate(bm,faces=list(bm.faces))
    bmesh.ops.delete(bm,geom=[f for f in bm.faces if f.calc_area()<1e-6],
                     context='FACES')
    bm.to_mesh(body.data);bm.free();body.data.update()
    for side in (-1,1):
        for label,y in [('F',WHEELS['front_z']),('R',WHEELS['rear_z'])]:
            anchor=bpy.data.objects.new('WHEEL_'+label+str(side),None)
            anchor.location=(side*WHEELS['x'],y,WHEELS['arch_y'])
            bpy.context.collection.objects.link(anchor)
    atlas=bpy.data.images.load(str(TEXTURE),check_existing=True)
    for slot in body.material_slots:
        mat=slot.material;mat.use_nodes=True
        node=mat.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas
        node.interpolation='Closest'
        shader=mat.node_tree.nodes.get('Principled BSDF')
        shader.inputs['Roughness'].default_value=.82
        mat.node_tree.links.new(node.outputs['Color'],shader.inputs['Base Color'])
    glass_mat=b.materials['GLASS']
    glass_mat.use_nodes=True
    glass_mat.surface_render_method='DITHERED'
    glass_shader=glass_mat.node_tree.nodes.get('Principled BSDF')
    glass_shader.inputs['Base Color'].default_value=(.085,.145,.175,1)
    glass_shader.inputs['Alpha'].default_value=.23
    glass_shader.inputs['Roughness'].default_value=.08
    args.mesh.parent.mkdir(parents=True,exist_ok=True)
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(args.blend))
    print('SADDLE_TANGO',export_emesh(body,args.mesh,'saddle_tango'))
    for name,group in glass_groups.items():
        bpy.ops.object.select_all(action='DESELECT')
        for obj in group:obj.select_set(True)
        bpy.context.view_layer.objects.active=group[0]
        if len(group)>1:bpy.ops.object.join()
        pane=bpy.context.view_layer.objects.active
        pane.name=name.upper()
        print('SADDLE_TANGO_GLASS',name,
              export_emesh(pane,MODEL/(name+'.emesh'),'saddle_tango_glass'))
    make_wheel()


if __name__=='__main__':main()
