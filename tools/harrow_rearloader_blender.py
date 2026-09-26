#!/usr/bin/env python3
"""Reproducible Harrow Rearloader source/cooked assets; reference-first design."""
import json, math, sys
from pathlib import Path
import bpy, bmesh
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from harrow_rearloader_spec import ROOT, SLUG, SHAPE as S, ATLAS, REGIONS, COLORS
from candidate_1991_geometry import bevel, finish, sheet, surround, rounded_loop, rod, lathe
from vesper_vx91_blender import VehicleBuilder, export_emesh


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    b=VehicleBuilder(); groups={'fixed':[],'driver':[]}
    panes={n:[] for n in ('windshield','rear_glass','passenger_glass','driver_glass')}
    model=ROOT/'assets/models/vehicles'/SLUG;model.mkdir(parents=True,exist_ok=True)
    for key,rgb in COLORS.items():b.material(key).diffuse_color=tuple(v/255 for v in rgb)+(1,)
    def keep(o,g='fixed'):
        groups[g].append(o);o['vehicle_group']=g;return o
    def box(n,lo,hi,m,g='fixed',r=.016):
        o=b.box(n,lo,hi,m)
        if r:bevel(o,min(r,min(hi[i]-lo[i] for i in range(3))*.45),3)
        return keep(o,g)
    def tube(n,a,c,r,m,g='fixed',segments=16):return keep(rod(b,n,a,c,r,m,segments),g)
    def skin(n,rows,m,g='fixed',thick=.035):return keep(sheet(b,n,rows,m,thick),g)
    def prism(n,poly,x0,x1,m,g='fixed'):
        k=len(poly);v=[(x,y,z) for x in (x0,x1) for y,z in poly]
        f=[tuple(reversed(range(k))),tuple(range(k,2*k))]+[(i,(i+1)%k,(i+1)%k+k,i+k) for i in range(k)]
        return keep(bevel(b.add_mesh(n,v,f,m),.012,2),g)
    def cut(o,c):
        bpy.context.view_layer.objects.active=o
        mod=o.modifiers.new('Authored aperture','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=c
        bpy.ops.object.modifier_apply(modifier=mod.name)
        b.objects.remove(c);bpy.data.objects.remove(c,do_unlink=True)
        # Keep the inward orientation of a closed cavity until door and window
        # holes connect it to the outer shell. Recalculating here refills it.
        o.data.update();return o
    def cutter(lo,hi,r=.02):
        o=b.box('CUTTER',lo,hi,'DARK')
        if r:bevel(o,r,4)
        return o
    def warp(o):
        # Common windshield/cab front rake, applied to adjoining shell and panes.
        for v in o.data.vertices:
            q=max(0,min(1,(v.co.y-3.00)/.50))
            v.co.y-=.245*max(0,min(1,(v.co.z-2.02)/1.10))*q
        return finish(o)
    def pane(name,corners):
        loop=rounded_loop(corners,.020,.065,8)
        normal=(Vector(corners[1])-Vector(corners[0])).cross(Vector(corners[2])-Vector(corners[0])).normalized()
        k=len(loop);vs=loop+[tuple(Vector(p)-normal*.003) for p in loop]
        fs=[tuple(range(k)),tuple(reversed(range(k,2*k)))]+[(i,(i+1)%k,(i+1)%k+k,i+k) for i in range(k)]
        o=b.add_mesh(name,vs,fs,'WINDOW');panes[name].append(o)
        o['vehicle_group']='driver' if name=='driver_glass' else 'fixed';return o
    # Frame rails, spring packs, axles, differential, tanks and access steps.
    for x in (-.66,.66):
        box('Longitudinal chassis rail',(x-.06,-3.4,.69),(x+.06,3.40,.94),'CLAD',r=.018)
    for y in (-3.10,-1.8,-.4,1.2,2.8):box('Chassis crossmember',(-.72,y-.055,.70),(.72,y+.055,.84),'CLAD')
    for axle in (S['front_axle'],S['rear_axle']):
        tube('Axle housing',(-1.03,axle,.60),(1.03,axle,.60),.09,'CLAD',segments=20)
        for x in (-.67,.67):
            for j in range(4):
                skin('Layered leaf spring', [[(x-.075,axle+y,.68+j*.018+.11*(y/.67)**2),(x+.075,axle+y,.68+j*.018+.11*(y/.67)**2)] for y in (-.67,-.45,0,.45,.67)],'STEEL',thick=.012)
        box('Differential housing',(-.20,axle-.20,.40),(.20,axle+.20,.78),'CLAD',r=.13)
    tube('Driveshaft',(0,-1.32,.67),(0,1.28,.67),.057,'STEEL')
    for side in (-1,1):
        x=side*.94
        box('Saddle fuel tank',(x-.19,-.21,.54),(x+.19,.83,1.03),'CLAD',r=.11)
        for y in (.01,.64):box('Tank retaining band',(x-.205,y-.020,.54),(x+.205,y+.020,1.04),'METAL',r=.009)
        box('Battery service box',(x-.18,.98,.63),(x+.18,1.34,1.03),'CLAD',r=.035)
        # Keep the whole step ahead of the tire envelope at full steering lock.
        assert S['cab_step_forward']-.23 > S['front_axle']+math.hypot(.602,.192)+.025
        for z,y in ((.47,S['cab_step_forward']),(.77,S['cab_step_forward'])):
            box('Cab non-slip access step',(side*1.20-.19,y-.23,z),(side*1.20+.19,y+.22,z+.06),'STEEL',r=.012)
            for j in range(9):box('Step grip', (side*1.20-.175,y-.20+j*.05,z+.057),(side*1.20+.175,y-.184+j*.05,z+.07),'DARK',r=.002)
    # One connected hollow cab shell. Apertures are real boolean openings.
    cab=box('Connected rounded cab shell',(-1.16,1.48,.91),(1.16,3.58,3.12),'CREAM',r=.105)
    cut(cab,cutter((-1.092,1.555,1.12),(1.092,3.505,3.055),.070))
    for side in (-1,1):
        a,c=sorted((side*1.045,side*1.35))
        cut(cab,cutter((a,1.645,.92),(c,3.225,3.055),.028))
        # Cab arch never cuts the entire floor across the truck.
        cut(cab,rod(b,'CUTTER',(side*.55,S['front_axle'],.60),(side*1.5,S['front_axle'],.60),.727,'DARK',64))
    for xa,xb in ((-1.02,-.033),(.033,1.02)):
        cut(cab,cutter((xa,3.25,2.055),(xb,3.80,3.015),.072))
    cut(cab,cutter((-.54,1.30,2.26),(.54,1.66,2.90),.055))
    warp(cab)
    # Split windscreen seal follows the same raked front surface as its shell.
    for xa,xb in ((-1.038,-.024),(.024,1.038)):
        corners=[(xa,3.583,2.038),(xb,3.583,2.038),(xb,3.583,3.03),(xa,3.583,3.03)]
        ring,_=surround(b,'Windscreen rubber gasket',corners,'DARK',.026,.014,.075);keep(warp(ring))
        warp(pane('windshield',corners))
    rear_corners=[(-.56,1.478,2.24),(.56,1.478,2.24),(.56,1.478,2.92),(-.56,1.478,2.92)]
    ring,_=surround(b,'Rear inspection window gasket',rear_corners,'DARK',.03,.015,.08);keep(ring);pane('rear_glass',rear_corners)
    # Doors: rounded solid stampings with real window apertures and arch cuts.
    for side in (-1,1):
        g='driver' if side>0 else 'fixed';xa,xb=sorted((side*1.095,side*1.162))
        door=box(('Driver' if side>0 else 'Passenger')+' connected door', (xa,1.653,.935),(xb,3.217,3.047),'CREAM',g,r=.028)
        cut(door,cutter((xa-.12,1.785,2.075),(xb+.12,3.107,2.972),.073))
        cut(door,rod(b,'CUTTER',(side*.86,S['front_axle'],.60),(side*1.48,S['front_axle'],.60),.737,'DARK',64))
        warp(door)
        corners=[(side*1.163,1.767,2.055),(side*1.163,3.125,2.055),(side*1.163,3.125,2.990),(side*1.163,1.767,2.990)]
        ring,_=surround(b,'Door glass rubber seal',corners,'DARK',.035,.015,.075);keep(warp(ring),g)
        pg='driver_glass' if side>0 else 'passenger_glass'
        glass=pane(pg,[(x-side*.019,y,z) for x,y,z in corners]);warp(glass)
        # Thick inner card clipped to the wheel arch, with mapped returns.
        a,c=sorted((side*1.071,side*1.096))
        card=box('Textured inner door card',(a,1.705,1.03),(c,3.145,2.023),'CABIN',g,r=.026)
        cut(card,rod(b,'CUTTER',(side*.82,S['front_axle'],.60),(side*1.3,S['front_axle'],.60),.755,'DARK',64));warp(card)
        a,c=sorted((side*.958,side*1.085))
        box('Inner armrest',(a,2.05,1.83),(c,2.70,1.91),'DARK',g,r=.03)
        box('Inner latch',(a,2.86,1.90),(c,3.035,1.96),'METAL',g,r=.013)
        a,c=sorted((side*1.16,side*1.195))
        box('Recessed exterior latch',(a,1.80,1.67),(c,1.96,1.80),'DARK',g,r=.026)
        for y in (3.17,):
            for z in (1.52,2.84):tube('Door hinge',(side*1.165,y,z),(side*1.165,y,z+.10),.021,'STEEL',g)
        # Common arch shoulder and dark inner return: no solid side at wheel center.
        for outer,m in ((True,'CREAM'),(False,'DARK')):
            x0=side*(1.17 if outer else .86);x1=side*(1.20 if outer else 1.11)
            rows=[]
            for i in range(33):
                t=math.pi*i/32
                rows.append([(x0,S['front_axle']+.750*math.cos(t),.60+.750*math.sin(t)),(x1,S['front_axle']+.800*math.cos(t),.60+.800*math.sin(t))])
            skin('Cab arch shoulder' if outer else 'Cab inner wheelhouse',rows,m,thick=.027)
        # Cab mirror is fitted to the movable door on the driver side.
        for z in (2.12,2.87):
            tube('Mirror stalk',(side*1.175,3.095,z),(side*1.43,3.095,z),.020,'DARK',g)
        tube('Mirror support',(side*1.43,3.095,2.12),(side*1.43,3.095,2.87),.021,'DARK',g)
        a,c=sorted((side*1.375,side*1.53))
        box('Main mirror housing',(a,3.02,2.30),(c,3.13,2.78),'DARK',g,r=.045)
        box('Mirror reflective face',(a+.012,3.015,2.325),(c-.012,3.026,2.755),'METAL',g,r=.032)
        box('Convex lower mirror',(a,3.016,2.13),(c,3.13,2.28),'DARK',g,r=.03)
    # Cab interior completely mapped; wheelhouse tops cover the exposed tire pockets.
    floor=box('Cab floor mat',(-1.065,1.60,1.11),(1.065,3.39,1.155),'CABIN')
    for side in (-1,1):cut(floor,rod(b,'CUTTER',(side*.55,S['front_axle'],.60),(side*1.30,S['front_axle'],.60),.75,'DARK',64))
    for side in (-1,1):
        a,c=sorted((side*.54,side*1.075))
        skin('Mapped interior wheelhouse', [[(a,S['front_axle']+.77*math.cos(t),.60+.77*math.sin(t)),(c,S['front_axle']+.77*math.cos(t),.60+.77*math.sin(t))] for t in [math.pi*i/24 for i in range(25)]],'CABIN',thick=.038)
    box('Headliner',(-1.04,1.62,3.024),(1.04,3.27,3.065),'HEADLINER',r=.035)
    box('Cab rear interior liner',(-1.075,1.553,1.15),(1.075,1.583,2.23),'CABIN',r=.018)
    box('Dashboard',(-1.075,2.95,1.85),(1.075,3.44,2.057),'DARK',r=.065)
    box('Engine doghouse',(-.27,1.67,1.15),(.27,2.84,1.56),'CABIN',r=.09)
    for side in (-1,1):
        x=side*.58
        box('Seat pedestal',(x-.21,1.90,1.15),(x+.21,2.37,1.48),'CLAD',r=.025)
        box('Seat cushion',(x-.29,1.85,1.48),(x+.29,2.47,1.63),'SEAT',r=.07)
        box('Seat back',(x-.285,1.80,1.57),(x+.285,1.99,2.28),'SEAT',r=.085)
        for xx in (x-.18,x,x+.18):tube('Seat fabric seam',(xx,1.789,1.67),(xx,1.789,2.17),.005,'DARK')
        box('Seat belt receiver',(x-.35,1.92,1.54),(x-.30,2.06,1.67),'DARK',r=.016)
    # Driver-left steering and period analog instruments with legible needles.
    steering=Vector((.58,2.82,2.09));axis=Vector((0,.70,-.72)).normalized()
    tube('Steering column',(.58,3.14,1.68),tuple(steering),.042,'DARK')
    u=Vector((1,0,0));v=axis.cross(u).normalized()
    for i in range(40):
        p=steering+.23*(u*math.cos(math.tau*i/40)+v*math.sin(math.tau*i/40));q=steering+.23*(u*math.cos(math.tau*(i+1)/40)+v*math.sin(math.tau*(i+1)/40))
        tube('Steering wheel rim',tuple(p),tuple(q),.021,'DARK',segments=8)
    for t in (0,math.tau/3,2*math.tau/3):tube('Steering spoke',tuple(steering),tuple(steering+.21*(u*math.cos(t)+v*math.sin(t))),.015,'STEEL')
    box('Instrument binnacle',(.25,3.025,2.01),(.93,3.31,2.18),'DARK',r=.065)
    for x in (.40,.68,.84):
        r=.057 if x<.8 else .032
        tube('Instrument bezel',(x,3.015,2.11),(x,3.025,2.11),r,'STEEL',segments=24)
        tube('Recessed black dial',(x,3.011,2.11),(x,3.016,2.11),r*.88,'DARK',segments=24)
        for j in range(9):
            t=math.radians(-130+j*32.5)
            a=(x+r*.68*math.sin(t),3.009,2.11+r*.68*math.cos(t))
            c=(x+r*.80*math.sin(t),3.009,2.11+r*.80*math.cos(t))
            tube('Gauge graduation',a,c,.0018,'HEADLINER',segments=6)
        t=math.radians(-100 if x<.5 else -25 if x<.8 else 35)
        tube('Gauge red needle',(x,3.006,2.11),(x+r*.70*math.sin(t),3.006,2.11+r*.70*math.cos(t)),.0028,'RED',segments=6)
    for x,m in ((.38,'RED'),(.46,'AMBER'),(.54,'AMBER')):
        box('Instrument telltale',(x,3.010,2.036),(x+.022,3.022,2.048),m,r=.002)
    for x in (-.50,-.38,-.26):
        box('Cab rocker switch',(x,2.938,1.956),(x+.045,2.952,2.020),'CLAD',r=.005)
        box('Switch legend',(x+.01,2.935,1.987),(x+.035,2.94,1.992),'HEADLINER',r=0)
    tube('Parking brake knob',(-.10,2.941,1.97),(-.10,2.909,1.97),.023,'AMBER',segments=12)
    for x in (.48,.69):box('Pedal',(x-.045,2.97,1.21),(x+.045,3.07,1.25),'DARK')
    tube('Gear lever',(.19,2.48,1.47),(.18,2.30,1.97),.018,'DARK')
    box('Gear knob',(.145,2.263,1.94),(.215,2.333,2.02),'DARK',r=.03)
    # Fascia is broad and flat with a restrained corner radius.
    box('Front steel bumper',(-1.22,3.59,.57),(1.22,3.83,.96),'CLAD',r=.045)
    box('Grille recess',(-.64,3.579,1.04),(.64,3.625,1.88),'DARK',r=.023)
    for j in range(10):box('Grille horizontal bar',(-.62,3.625,1.085+j*.073),(.62,3.648,1.103+j*.073),'STEEL',r=.003)
    for x in (-.42,0,.42):box('Grille vertical support',(x-.008,3.644,1.085),(x+.008,3.653,1.80),'CLAD',r=.003)
    for x in (-.065,.065):box('Steel-beam H upright',(x-.016,3.66,1.39),(x+.016,3.69,1.63),'METAL',r=.004)
    box('Steel-beam H crossbar',(-.08,3.66,1.49),(.08,3.69,1.535),'METAL',r=.004)
    for side in (-1,1):
        x=side*.91
        box('Headlamp gasket',(x-.21,3.588,1.017),(x+.21,3.655,1.312),'DARK',r=.03)
        box('Sealed beam lens',(x-.16,3.651,1.057),(x+.16,3.67,1.281),'LAMP',r=.018)
        xx=side*1.13
        box('Corner amber indicator',(xx-.06,3.583,1.037),(xx+.06,3.645,1.297),'AMBER',r=.016)
        for z in (.69,.85):tube('Bumper bolt',(side*1.09,3.827,z),(side*1.09,3.84,z),.013,'METAL',segments=6)
        # Wipers on the screen slope.
        for a,c in (((side*.20,3.59,2.01),(side*.58,3.47,2.49)),((side*.58,3.47,2.43),(side*.81,3.33,2.89))):tube('Wiper arm',a,c,.013,'DARK',segments=10)
        tube('Amber beacon base',(side*.89,3.10,3.13),(side*.89,3.10,3.19),.102,'DARK',segments=24)
        tube('Amber beacon lens',(side*.89,3.10,3.19),(side*.89,3.10,3.36),.085,'AMBER',segments=24)
    for x in (-.29,0,.29):box('Cab roof marker',(x-.045,3.22,3.12),(x+.045,3.32,3.17),'AMBER',r=.022)
    # Refuse container, a broad closed loft with round shoulder cross sections.
    ring=[(-1.15,1.38),(-1.20,1.43),(-1.20,3.28),(-1.185,3.40),(-1.13,3.49),(-1.02,3.54),(-.84,3.55),(.84,3.55),(1.02,3.54),(1.13,3.49),(1.185,3.40),(1.20,3.28),(1.20,1.43),(1.15,1.38)]
    keep(finish(b.loft('Pressed refuse container',[(y,ring) for y in (-2.10,-1.9,1.25,1.40)],'PAINT')))
    for side in (-1,1):
        for y in (-1.39,-.48,.43,1.32):
            a,c=sorted((side*1.18,side*1.245))
            box('Container side rib',(a,y-.055,1.47),(c,y+.055,3.31),'PAINT',r=.020)
        a,c=sorted((side*1.19,side*1.26))
        box('Container lower sill',(a,-2.09,1.37),(c,1.41,1.60),'PAINT',r=.025)
        box('Rear fender skirt',(side*.99-.27,-2.32,1.29),(side*.99+.27,-.73,1.39),'CLAD',r=.07)
        skin('Rear formed wheel fender',[[(side*.70,S['rear_axle']+.79*math.cos(t),.60+.79*math.sin(t)),(side*1.31,S['rear_axle']+.79*math.cos(t),.60+.79*math.sin(t))] for t in [math.pi*i/28 for i in range(29)]],'CLAD',thick=.044)
        a,c=sorted((side*.76,side*1.30));box('Rear mudflap',(a,-2.32,.32),(c,-2.29,1.18),'DARK',r=.006)
        for y in (-1.89,1.23):box('Body side amber marker',(side*1.254-.02,y-.067,1.53),(side*1.254+.02,y+.067,1.59),'AMBER',r=.006)
        a,c=sorted((side*1.20,side*1.225));box('Inspection access hatch',(a,-1.96,1.75),(c,-1.42,1.96),'PAINT',r=.010)
    # Roof beams are shallow stamped channels, not giant ribs.
    for x in (-.82,0,.82):box('Refuse roof channel',(x-.035,-2.06,3.535),(x+.035,1.36,3.585),'PAINT',r=.012)
    # Rear packer side cheek plates enclose a genuine open loading throat.
    cheek=[(-2.06,1.41),(-2.06,3.48),(-2.46,3.41),(-3.53,2.76),(-3.70,1.08),(-3.37,.88),(-2.60,.88),(-2.06,1.35)]
    for side in (-1,1):
        a,c=sorted((side*.99,side*1.20));prism('Packer cheek structure',cheek,a,c,'PAINT')
        a,c=sorted((side*.97,side*1.005));prism('Mapped hopper inside cheek',cheek,a,c,'WEAR')
    skin('Hopper curved inner trough',[[(-.98,y,z),(.98,y,z)] for y,z in ((-2.10,1.75),(-2.37,1.25),(-2.63,.99),(-2.98,.92),(-3.36,.95),(-3.68,1.10))],'WEAR',thick=.07)
    box('Hopper loading sill',(-1.02,-3.74,1.015),(1.02,-3.55,1.18),'STEEL',r=.040)
    for z in (1.06,1.14):box('Loading sill wear rail',(-.99,-3.752,z),(.99,-3.724,z+.028),'METAL',r=.009)
    skin('Packer blade face',[[(-.96,-2.22,2.96),(.96,-2.22,2.96)],[(-.96,-2.45,2.48),(.96,-2.45,2.48)],[(-.96,-2.95,2.17),(.96,-2.95,2.17)]],'WEAR',thick=.10)
    tube('Packer lower blade beam',(-.99,-2.95,2.17),(.99,-2.95,2.17),.065,'STEEL',segments=20)
    box('Packer top crossbeam',(-1.12,-2.48,3.13),(1.12,-2.27,3.35),'PAINT',r=.025)
    for side in (-1,1):
        x=side*1.245
        a=Vector((x,-2.16,3.36));c=Vector((x,-3.40,1.17));d=c-a
        tube('Hydraulic chrome piston',tuple(a),tuple(a+d*.53),.045,'METAL',segments=24)
        tube('Hydraulic green barrel',tuple(a+d*.48),tuple(c),.082,'PAINT',segments=24)
        for t in (.49,.95):tube('Ram collar',tuple(a+d*t),tuple(a+d*(t+.035)),.102,'STEEL',segments=24)
        for p in (a,c):
            tube('Ram pivot eye',(p.x-.085,p.y,p.z),(p.x+.085,p.y,p.z),.115,'PAINT',segments=24)
            tube('Pivot steel pin',(p.x-.11,p.y,p.z),(p.x+.11,p.y,p.z),.052,'METAL',segments=20)
        # Secondary packer linkage and dark hydraulic hose connected to barrel.
        tube('Packer link upper',(side*1.16,-2.12,3.39),(side*1.16,-2.83,2.42),.055,'PAINT')
        tube('Packer link lower',(side*1.16,-2.83,2.42),(side*1.16,-3.43,1.30),.057,'PAINT')
        hose=[(x+.055*side,-2.17,3.39),(x+.09*side,-2.25,3.48),(x+.11*side,-2.37,3.38),(x+.11*side,-2.63,2.78),(x+.11*side,-3.32,1.48),(x+.04*side,-3.43,1.33)]
        for j in range(len(hose)-1):tube('Connected hydraulic hose',hose[j],hose[j+1],.019,'DARK',segments=10)
        a,c=sorted((side*.97,side*1.37))
        box('Rear worker step',(a,-3.83,.68),(c,-3.38,.75),'STEEL',r=.012)
        for j in range(8):box('Worker step tread',(a+.025,-3.80+j*.049,.751),(c-.025,-3.786+j*.049,.765),'DARK',r=.002)
        rail=[(side*1.31,-3.54,.74),(side*1.31,-3.54,2.63),(side*1.31,-3.39,2.70),(side*1.31,-3.23,2.64)]
        for j in range(len(rail)-1):tube('Worker grab rail',rail[j],rail[j+1],.024,'PAINT')
        for zz in (1.45,1.91):tube('Grab rail side rung',(side*1.31,-3.54,zz),(side*1.31,-3.22,zz),.020,'PAINT')
        a,c=sorted((side*1.03,side*1.17))
        for lo,hi in ((1.01,1.53),(2.98,3.31)):
            lamp_y=-3.714 if lo<2 else -2.51
            lamp_a,lamp_c=(a,c) if lo<2 else sorted((side*.77,side*.92))
            box('Rear lamp housing',(lamp_a-.025,lamp_y,lo),(lamp_c+.025,lamp_y+.124,hi),'DARK',r=.020)
            n=3 if lo<2 else 2
            for j in range(n):
                z=lo+.025+j*(hi-lo-.02)/n;m=('LAMP','RED','AMBER')[j] if n==3 else ('RED','AMBER')[j]
                box('Rear lamp lens',(lamp_a,lamp_y-.014,z),(lamp_c,lamp_y+.004,z+(hi-lo)/n-.038),m,r=.016)
        # Red/cream hazard bars are geometry and all reverse faces have material.
        a,c=sorted((side*1.04,side*1.19))
        box('Rear reflective warning board',(a,-3.66,1.63),(c,-3.61,2.31),'CREAM',r=.006)
        for j in range(4):
            z=1.66+j*.16
            prism('Red hazard stripe',[(-3.669,z),(-3.669,z+.085),(-3.679,z+.085),(-3.679,z)],a,c,'RED')
    box('Rear underride beam',(-1.15,-3.81,.64),(1.15,-3.61,.87),'CLAD',r=.024)
    # Blank plate mounts, including their visible inner backings.
    for y,sgn in ((3.835,1),(-3.823,-1)):
        box('Plate recessed backing',(-.171,y-.009,.705),(.171,y+.009,.875),'DARK',r=.014)
        for x in (-.171,.171):box('Plate raised border',(x-.010,y-.014,.701),(x+.010,y+.014,.879),'STEEL',r=.004)
        for z in (.701,.879):box('Plate raised border',(-.18,y-.014,z-.009),(.18,y+.014,z+.009),'STEEL',r=.004)
    # Regressions from the first review: cabin must be open through the door
    # aperture and through both windshield openings, not a black inner wall.
    for y,z in ((2.4,2.5),(2.4,1.8)):
        assert not cab.ray_cast(Vector((3,y,z)),Vector((-1,0,0)))[0], 'Cab door aperture blocked'
    for x in (-.5,.5):
        assert not cab.ray_cast(Vector((x,2.70,2.50)),Vector((0,1,0)))[0], 'Cab windshield blocked'
    # Semantic atlas UVs assigned only after topology is final.
    def uvmap(o):
        layer=o.data.uv_layers.new(name='Atlas')
        mins=[min(v.co[i] for v in o.data.vertices) for i in range(3)];maxs=[max(v.co[i] for v in o.data.vertices) for i in range(3)]
        for p in o.data.polygons:
            key=o.data.materials[p.material_index].name;x0,y0,x1,y1=REGIONS[key]
            axes=(1,2) if abs(p.normal.x)>.60 else (0,2) if abs(p.normal.y)>.60 else (0,1)
            for li in p.loop_indices:
                v=o.data.vertices[o.data.loops[li].vertex_index].co
                q=[max(0,min(1,(v[a]-mins[a])/(maxs[a]-mins[a]))) if maxs[a]-mins[a]>1e-8 else .5 for a in axes]
                layer.data[li].uv=((x0+2+q[0]*60)/ATLAS,1-(y1-2-q[1]*60)/ATLAS)
    for o in list(b.objects):uvmap(o)
    def export(name,objs):
        bpy.ops.object.select_all(action='DESELECT');copies=[]
        for o in objs:
            c=o.copy();c.data=o.data.copy();bpy.context.collection.objects.link(c);c.select_set(True);copies.append(c)
        bpy.context.view_layer.objects.active=copies[0]
        if len(copies)>1:bpy.ops.object.join()
        joined=bpy.context.object
        # Degenerate boolean sliver removal after triangulation, with no shape loss.
        bm=bmesh.new();bm.from_mesh(joined.data);bmesh.ops.triangulate(bm,faces=list(bm.faces))
        bad=[f for f in bm.faces if f.calc_area()<1e-10]
        if bad:bmesh.ops.delete(bm,geom=bad,context='FACES')
        bm.to_mesh(joined.data);bm.free()
        result=export_emesh(joined,model/(name+'.emesh'),SLUG);bpy.data.objects.remove(joined,do_unlink=True);return result
    report={}
    report['body']=export('body',groups['fixed']+groups['driver']);report['body_open']=export('body_open',groups['fixed']);report['driver_door']=export('driver_door',groups['driver'])
    for n,os in panes.items():report[n]=export(n,os)
    def wheels(name,centers):
        parts=[]
        def add(o):uvmap(o);parts.append(o);return o
        for cx in centers:
            half=.15 if len(centers)==1 else .125;r=.60
            profile=[(-half*.72,.32),(-half,.43),(-half*.97,.52),(-half*.68,.583),(-half*.4,r),(half*.4,r),(half*.68,.583),(half*.97,.52),(half,.43),(half*.72,.32)]
            add(lathe(b,name+' tire',[(cx+x,rr) for x,rr in profile],'TYRE',56))
            for sign in (-1,1):
                x=cx+sign*(half+.004)
                profile=[(x-sign*.025,.33),(x,.39),(x+sign*.007,.39),(x+sign*.012,.355),(x-sign*.045,.30),(x-sign*.045,.13),(x+sign*.025,.10),(x+sign*.025,.012)]
                add(lathe(b,name+' steel rim',profile,'STEEL',48))
                for j in range(10):
                    t=math.tau*j/10;y=.274*math.cos(t);z=.274*math.sin(t)
                    add(rod(b,name+' rim vent',(x-sign*.041,y,z),(x-sign*.038,y,z),.034,'DARK',12))
                    y=.14*math.cos(t);z=.14*math.sin(t)
                    add(rod(b,name+' lug',(x-sign*.036,y,z),(x+sign*.017,y,z),.017,'METAL',6))
                add(rod(b,name+' hub',(x,0,0),(x+sign*.038,0,0),.091,'CLAD',20))
            for j in range(48):
                t=math.tau*j/48
                for shoulder in (-1,1):
                    x=cx+shoulder*half*.35
                    vs=[(x+dx,rr*math.cos(t+da),rr*math.sin(t+da)) for rr in (.596,.602) for dx,da in ((-.030,-.018),(.030,-.009),(.030,.032),(-.030,.023))]
                    add(b.add_mesh(name+' tread block',vs,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],'TYRE'))
        return parts
    front=wheels('Front wheel',[0]);rear=wheels('Rear dual wheel',S['rear_dual_offsets'])
    report['front_wheel']=export('front_wheel',front);report['rear_wheel']=export('rear_wheel',rear)
    for o in front+rear:o.hide_render=True;o.hide_set(True)
    for y,parts in ((S['front_axle'],front),(S['rear_axle'],rear)):
        for side in (-1,1):
            for o in parts:
                c=o.copy();c.data=o.data.copy();bpy.context.collection.objects.link(c);c.location=(side*S['wheel_x'],y,.60);c.hide_render=False;c.hide_set(False);c['vehicle_group']='wheel';c.name=o.name+(' left' if side>0 else ' right')
    atlas=bpy.data.images.load(str(ROOT/'assets/textures/vehicles'/SLUG/'body.png'));atlas.pack()
    for key,m in b.materials.items():
        m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');node=m.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas;node.interpolation='Closest'
        m.node_tree.links.new(node.outputs['Color'],p.inputs['Base Color']);p.inputs['Roughness'].default_value=.53 if key in ('PAINT','CREAM','TOP') else .86
        if key in ('METAL','STEEL'):p.inputs['Metallic'].default_value=.45;p.inputs['Roughness'].default_value=.40
        if key=='TYRE':p.inputs['Specular IOR Level'].default_value=0
        if key=='WINDOW':
            m.node_tree.links.remove(p.inputs['Base Color'].links[0]);p.inputs['Base Color'].default_value=(.70,.82,.82,1);p.inputs['Transmission Weight'].default_value=.95;p.inputs['Alpha'].default_value=1.0;p.inputs['Roughness'].default_value=.10
    for key in ('driver_seat','steering_wheel','driver_hinge','step_exit','plate_front','plate_rear'):
        x,y,z=S[key];o=bpy.data.objects.new('ANCHOR_'+key,None);bpy.context.collection.objects.link(o);o.location=(x,z,y);o.empty_display_type='ARROWS';o.empty_display_size=.15
    for axle in ('front_axle','rear_axle'):
        for side in (-1,1):
            o=bpy.data.objects.new('ANCHOR_'+axle+('_left' if side>0 else '_right'),None);bpy.context.collection.objects.link(o);o.location=(side*S['wheel_x'],S[axle],S['wheel_radius']);o.empty_display_type='CIRCLE';o.empty_display_size=.60
    bpy.ops.wm.save_as_mainfile(filepath=str(model/'source.blend'))
    (model/'anchors.json').write_text(json.dumps(S,indent=2)+'\n')
    report={'identity':'Harrow Rearloader','quality':'Target: Class 3; review pending','shape':S,'parts':report}
    (ROOT/'build/harrow_rearloader-fit-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('HARROW_REARLOADER',json.dumps(report))
if __name__=='__main__':build()
