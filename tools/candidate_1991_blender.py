#!/usr/bin/env python3
"""Reference-led, rounded 1991 vehicle surfaces and detailed movable cabins."""
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from candidate_1991_spec import ATLAS, REGIONS, ROOT, SHAPES
from candidate_1991_geometry import bevel, finish, sheet, surround, rounded_loop, rod, lathe
from vesper_vx91_blender import VehicleBuilder, export_emesh


def build(slug):
    s=SHAPES[slug];kind=s['kind']
    model=ROOT/'assets/models/vehicles'/slug;model.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    b=VehicleBuilder();fixed=[];driver=[]
    panes={name:[] for name in ('windshield','rear_glass','passenger_glass',
                               'driver_glass','driver_rear_glass','passenger_rear_glass')}
    palette={'PAINT':s['paint'],'TOP':s['top'],'CREAM':s['cream'],'CLAD':s['clad'],
             'DARK':(22,26,28),'CABIN':(66,68,68),'SEAT':s['seat'],
             'HEADLINER':(152,152,140),'METAL':(168,173,168),'LAMP':(229,228,204),
             'AMBER':(224,130,32),'RED':(174,35,30),'WINDOW':(76,100,107),
             'TYRE':(25,26,27),'STEEL':(176,178,172),'STRIPE':(27,28,27)}
    for key,rgb in palette.items():
        material=b.material(key);material.diffuse_color=tuple(c/255 for c in rgb)+(1,)
        material.roughness=.48 if key in ('PAINT','TOP','CREAM') else .95
    def keep(obj,group='fixed'):
        obj['vehicle_group']=group
        (driver if group=='driver' else fixed).append(obj);return obj
    def mesh(name,vertices,faces,material,group='fixed'):
        return keep(finish(b.add_mesh(name,vertices,faces,material)),group)
    def box(name,lo,hi,material,group='fixed',rounding=.018):
        obj=b.box(name,lo,hi,material)
        if rounding:bevel(obj,min(rounding,min(hi[i]-lo[i] for i in range(3))*.45),3)
        return keep(obj,group)
    def tube(name,a,c,r,material,group='fixed',segments=16):
        return keep(rod(b,name,a,c,r,material,segments),group)
    def beam(name,a,c,width,material,group='fixed',depth=None):
        axis=(Vector(c)-Vector(a)).normalized();u=axis.cross(Vector((0,0,1)))
        if u.length<.001:u=axis.cross(Vector((1,0,0)))
        u.normalize();v=axis.cross(u).normalized();depth=depth or width
        verts=[tuple(Vector(p)+su*u*width*.5+sv*v*depth*.5) for p in (a,c)
               for su,sv in ((-1,-1),(1,-1),(1,1),(-1,1))]
        if 'seam' in name.lower():
            return mesh(name,verts,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],material,group)
        return keep(bevel(b.add_mesh(name,verts,[(0,3,2,1),(4,5,6,7),(0,1,5,4),
                    (1,2,6,5),(2,3,7,6),(3,0,4,7)],material),min(width,depth)*.12,2),group)
    def skin(name,rows,material,group='fixed',thickness=.025):
        return keep(sheet(b,name,rows,material,thickness),group)
    def pane(name,loop):
        normal=(Vector(loop[1])-Vector(loop[0])).cross(Vector(loop[len(loop)//2 if len(loop)>3 else 2])-Vector(loop[0])).normalized()*.0015
        n=len(loop);verts=[tuple(Vector(p)+normal) for p in loop]+[tuple(Vector(p)-normal) for p in loop]
        faces=[tuple(range(n)),tuple(reversed(range(n,2*n)))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
        obj=b.add_mesh(name,verts,faces,'WINDOW')
        obj['surface_normal']=tuple(normal.normalized())
        obj['vehicle_group']='driver' if name=='driver_glass' else 'fixed'
        panes[name].append(obj);return obj
    def window(name,corners,category,material='PAINT',group='fixed'):
        border=.045 if name in ('Windshield','Rear window') else .055
        ring,_=surround(b,name+' body surround',corners,material,border,.024,.009)
        keep(ring,group)
        surface_normal=(Vector(corners[1])-Vector(corners[0])).cross(Vector(corners[2])-Vector(corners[0])).normalized()
        ring['surface_normal']=tuple(surface_normal)
        inset=rounded_loop(corners,border-.009,.070)
        # A continuous rubber seal borders the clear opening.
        n=len(inset)
        inner=rounded_loop(corners,border+.008,.061)
        seal=mesh(name+' rubber seal',inset+inner,[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)],'DARK',group)
        seal['surface_normal']=tuple(surface_normal)
        pane(category,rounded_loop(corners,border+.005,.064))
    width=s['half_width'];length=s['half_length'];belt=s['belt'];roof=s['roof']
    radius=s['wheel_radius'];arch=s['arch_radius']
    front_axle=s['front_axle'];rear_axle=s['rear_axle'];sill=s['door_sill']
    def xr(a,c,sign):return min(sign*a,sign*c),max(sign*a,sign*c)
    def plan_width(y):
        # Pressed corners taper in plan; broad doors retain full shoulder width.
        end=max(0,(abs(y)-(length-.36))/.36)
        return width-.045*end*end
    def side_top(y):
        return (1.44 if y<s['roof_rear'] else belt) if kind=='wrecker' else belt if y<=s['windshield_base'] else belt-(.035 if kind=='suv' else .13)*(y-s['windshield_base'])/(length-s['windshield_base'])
    def arch_bottom(y):
        base=.30 if kind=='van' else .37 if kind=='suv' else .40
        for axle in (rear_axle,front_axle):
            d=y-axle
            if abs(d)<arch:base=max(base,radius+.035+math.sqrt(max(0,arch*arch-d*d)))
        return base
    fractions=(0,.07,.24,.52,.78,.94,1)
    bulges=(-.045,-.020,.015,.035,.028,-.008,-.02) if kind=='van' else (-.045,-.015,.008,.018,.012,-.013,-.02)
    def side_x(y,t):
        # Shared by the body, trim and solid inner door surface.
        for i in range(len(fractions)-1):
            if t<=fractions[i+1]:
                u=(t-fractions[i])/(fractions[i+1]-fractions[i])
                if kind=='van':u=u*u*(3-2*u)
                return plan_width(y)+bulges[i]*(1-u)+bulges[i+1]*u
        return plan_width(y)+bulges[-1]
    box('Chassis floor',(-width*.68,-length+.19,.30),(width*.68,length-.20,s['cab_floor']),'CLAD')
    box('Mapped cabin carpet',(-width*.79,s['roof_rear']+.08,s['cab_floor']),
        (width*.79,s['windshield_base']-.19,s['cab_floor']+.035),'CABIN')
    end_side=length-.14
    stations={-end_side+i*2*end_side/76 for i in range(77)}
    stations.update((s['door_rear'],s['door_front'],s['windshield_base']))
    for axle in (front_axle,rear_axle):
        stations.update(axle-arch*math.cos(math.pi*i/24) for i in range(25))
    stations=sorted(y for y in stations if -end_side<=y<=end_side)
    for sign in (-1,1):
        for label,lo,hi in (('rear quarter',-end_side,s['door_rear']),
                            ('front door',s['door_rear'],s['door_front']),
                            ('front fender',s['door_front'],end_side)):
            ys=[y for y in stations if lo-.00001<=y<=hi+.00001]
            group='driver' if sign>0 and label=='front door' else 'fixed'
            rows=[]
            for y in ys:
                low=arch_bottom(y);top=side_top(y)
                row=[]
                for t in ([i/10 for i in range(11)] if kind=='van' else fractions):
                    z=low+(top-low)*t
                    global_t=max(0,min(1,(z-sill)/(top-sill)))
                    row.append((sign*side_x(y,global_t),y,z))
                rows.append(row)
            skin(('Left ' if sign>0 else 'Right ')+label+' curved stamping',rows,'PAINT',group,.045)
            if label=='front door':
                rows=[[(x-sign*.072,y,z+.012) for x,y,z in row] for row in rows]
                skin('Upholstered inner door card',rows,'CABIN',group,.018)
                # Recessed pull and armrest on the visible inner face.
                a,c=xr(width-.20,width-.09,sign)
                box('Door armrest',(a,lo+.18,belt-.33),(c,hi-.17,belt-.25),'DARK',group,.04)
                box('Inner latch',(a,lo+.23,belt-.21),(c,lo+.37,belt-.155),'METAL',group,.012)
    for sign in (-1,1):
        for axle in (rear_axle,front_axle):
            angles={math.pi*i/32 for i in range(33)}
            for seam in (s['door_rear'],s['door_front']):
                q=(seam-axle)/arch
                if -1<q<1:angles.add(math.acos(q))
            runs=[]
            for t in sorted(angles):
                y=axle+arch*math.cos(t);z=radius+.035+arch*math.sin(t)
                runs.append((y,[(sign*(plan_width(y)-.015),y,z),
                    (sign*(plan_width(y)+.021),axle+(arch+.027)*math.cos(t),radius+.035+(arch+.027)*math.sin(t)),
                    (sign*(plan_width(y)+.028),axle+(arch+.052)*math.cos(t),radius+.035+(arch+.052)*math.sin(t)),
                    (sign*(plan_width(y)+.003),axle+(arch+.072)*math.cos(t),radius+.035+(arch+.072)*math.sin(t))]))
            start=0
            while start<len(runs)-1:
                mid=(runs[start][0]+runs[start+1][0])*.5
                moving=sign>0 and s['door_rear']<=mid<=s['door_front']
                end=start+1
                while end<len(runs)-1:
                    m=(runs[end][0]+runs[end+1][0])*.5
                    if (sign>0 and s['door_rear']<=m<=s['door_front'])!=moving:break
                    end+=1
                skin('Pressed fender arch',[row for _,row in runs[start:end+1]],
                     'CLAD' if kind=='suv' else 'PAINT','driver' if moving else 'fixed',.012)
                start=end
            # Dark curved wheel tub: the outer opening stays clear.
            skin('Wheelhouse inner return',[[
                (sign*(width-.065),axle+(arch+.015)*math.cos(t),radius+.035+(arch+.015)*math.sin(t)),
                (sign*(width-.24),axle+(arch+.015)*math.cos(t),radius+.035+(arch+.015)*math.sin(t))]
                for t in [math.pi*i/24 for i in range(25)]],'DARK',thickness=.012)
    # Crowned hood joins the matching fender edge; nose height falls toward lamps.
    if kind!='wrecker':
        rows=[]
        for j in range(13):
            y=s['windshield_base']+(length-s['windshield_base'])*j/12
            row=[]
            for i in range(25):
                u=-1+2*i/24;yy=y-.14*abs(u)**8*(j/12)**6
                crown=.085*(1-u**4) if kind=='van' else .065*(1-u*u)
                row.append((side_x(yy,1)*u,yy,side_top(yy)+crown))
            rows.append(row)
        skin('Crowned sloping hood',rows,'PAINT',thickness=.045)
    # Wrap-around fascia and bumpers have authored rounded corner sections.
    def end_panel(name,end,zlo,zhi,material,projection=0,thickness=.08):
        rows=[]
        bumper='bumper' in name
        for j in range(33):
            u=-1+2*j/32
            wrap=.14*abs(u)**8
            y=end*(length+projection-wrap)
            w=(width+.04) if bumper else plan_width(y)-.02
            x=u*w
            top=zhi if bumper or end<0 else side_top(y)+(.085*(1-u**4) if kind=='van' else .065*(1-u*u))
            if kind=='van' and bumper:
                # Rolled bumper edges: a broad flat face with circular returns.
                r=.065
                rows.append([(x,y-end*r,zlo),
                             (x,y-end*r*(1-.5),zlo+r*(1-.866)),
                             (x,y-end*r*(1-.866),zlo+r*.5),
                             (x,y,zlo+r), (x,y,top-r),
                             (x,y-end*r*(1-.866),top-r*.5),
                             (x,y-end*r*(1-.5),top-r*(1-.866)),
                             (x,y-end*r,top)])
            else:
                rows.append([(x,y-end*.025,zlo+.018),(x,y,zlo+.055),
                             (x,y,top-.036),(x,y-end*.004,top)])
        skin(name,rows,material,thickness=thickness)
    fascia_top=side_top(length)+.02
    end_panel('Sculpted front valance',1,.50,fascia_top,'PAINT',-.004)
    end_panel('Rear hatch lower panel',-1,.50,belt,'PAINT',-.012)
    end_panel('Wrapped front bumper',1,.34,.61 if kind=='van' else .64,'CLAD',.07,.09)
    end_panel('Wrapped rear bumper',-1,.34,.57,'CLAD',.065,.09)
    # Separate brand faces: wide twin-optic van lamps, square SUV sealed beams,
    # and taller industrial truck lights.
    lamp_z=.79 if kind=='van' else .82 if kind=='suv' else .86
    lamp_h=.21 if kind=='van' else .27 if kind=='suv' else .29
    lamp_inner=.46 if kind=='van' else .50
    grille_half=.425 if kind=='van' else .465
    box('Recessed grille pocket',(-grille_half,length+.005,lamp_z-.02),
        (grille_half,length+.022,lamp_z+lamp_h-.02),'DARK',rounding=.014)
    for i in range(2 if kind=='van' else 3):
        z=lamp_z+.025+i*(lamp_h-.06)/(1 if kind=='van' else 2)
        box('Grille horizontal fin',(-grille_half+.018,length+.023,z),
            (grille_half-.018,length+.043,z+.012),'PAINT' if kind=='wrecker' else 'CLAD',rounding=.004)
    for x in (-.28,0,.28):
        box('Grille divider',(x-.007,length+.025,lamp_z),
            (x+.007,length+.038,lamp_z+lamp_h-.05),'DARK',rounding=.003)
    for sign in (-1,1):
        a,c=xr(lamp_inner,width-.10,sign)
        box('Headlamp recessed bezel',(a-.016,length+.010,lamp_z-.019),
            (c+.016,length+.020,lamp_z+lamp_h+.016),'DARK',rounding=.025)
        box('Ribbed headlamp lens',(a,length+.018,lamp_z),
            (c,length+.028,lamp_z+lamp_h),'LAMP',rounding=.023)
        if kind=='van':
            x=(a+c)*.5
            box('Twin optic divider',(x-.006,length+.029,lamp_z+.014),
                (x+.006,length+.032,lamp_z+lamp_h-.014),'METAL',rounding=.003)
        a,c=xr(width-.095,width-.020,sign)
        box('Corner indicator',(a,length-.018,lamp_z),(c,length+.030,lamp_z+lamp_h),'AMBER',rounding=.028)
        a,c=xr(.54,.74,sign)
        box('Bumper vent or fog recess',(a,length+.074,.415),(c,length+.084,.535),'DARK',rounding=.022)
        if kind!='van':box('Amber bumper lens',(a+.019,length+.085,.432),
                          (c-.019,length+.092,.517),'AMBER',rounding=.011)
        # Tall rear combination lamp with distinct lens cells and housing.
        a,c=xr(width-.125,width-.013,sign)
        box('Rear lamp gasket',(a-.016,-length-.038,.60),(c+.016,-length+.020,1.14),'DARK',rounding=.025)
        for z,h,mat in ((.62,.25,'RED'),(.89,.12,'AMBER'),(1.025,.085,'LAMP')):
            box('Rear combination lens',(a,-length-.049,z),(c,-length-.037,z+h),mat,rounding=.012)
    box('Lower radiator inlet',(-.42,length+.080,.405),(.42,length+.087,.49),'DARK',rounding=.025)
    if kind=='van':
        from meridian_cabin import build_cabin
        build_cabin(b,s,keep,mesh,skin,panes)
    else:
        # Domed roof; matching lower shell supplies an intentional headliner.
        def roof_panel(name,ya,yb,material):
            rows=[]
            for j in range(17):
                t=j/16;y=ya+(yb-ya)*t
                longitudinal=.022*math.sin(math.pi*t)
                rows.append([((width-.10)*u,y,
                              roof-.038+.037*(1-u*u)+longitudinal*.45)
                             for u in [-1+2*i/24 for i in range(25)]])
            skin(name,rows,material,thickness=.045)
            skin(name+' headliner',[[(x*.97,y,z-.052) for x,y,z in row] for row in rows],'HEADLINER',thickness=.014)
            # Header and side returns use the roof's exact boundary stations. This
            # closes the daylight slit without filling any window aperture.
            for edge in (rows[0],rows[-1]):
                skin(name+' curved roof header',[[p,(p[0],p[1],roof-.045)] for p in edge],material,thickness=.022)
            for index in (0,-1):
                skin(name+' upper window rail',[[row[index],(row[index][0],row[index][1],roof-.045)]
                      for row in rows],material,thickness=.018)

        if kind=='suv':
            roof_panel('Cab roof',-.32,s['roof_front'],'PAINT')
            roof_panel('Removable hardtop',s['roof_rear'],-.31,'CREAM')
        else:roof_panel('Crowned cabin roof',s['roof_rear'],s['roof_front'],'CREAM' if kind=='wrecker' else 'PAINT')
        wind=[(-width+.06,s['windshield_base'],belt+.012),(width-.06,s['windshield_base'],belt+.012),
              (width-.10,s['roof_front'],roof-.043),(-width+.10,s['roof_front'],roof-.043)]
        window('Windshield',wind,'windshield')
    # Wipers sit on the actual sloped screen, each with a pivot and blade.
    for x in (-.41,.41):
        tube('Wiper pivot',(x,s['windshield_base']+.016,belt+.05),
             (x,s['windshield_base']+.034,belt+.075),.025,'DARK')
        beam('Wiper arm',(x,s['windshield_base']+.026,belt+.07),
             (x-.15,s['windshield_base']-.035,belt+.135),.016,'DARK')
        beam('Wiper blade',(x-.34,s['windshield_base']-.036,belt+.137),
             (x+.10,s['windshield_base']-.036,belt+.137),.022,'DARK')
    if kind!='van':
        back_y=-length+.04 if kind!='wrecker' else s['roof_rear']
        back=[(-width+.06,back_y,belt+.025),(width-.06,back_y,belt+.025),
              (width-.10,s['roof_rear'],roof-.047),(-width+.10,s['roof_rear'],roof-.047)]
        window('Rear window',back,'rear_glass','CREAM' if kind=='suv' else 'PAINT')
    for sign in (-1,1):
        group='driver' if sign>0 else 'fixed'
        if kind!='van':
            def side_corners(ya,yb):
                return [(sign*(width-.04),ya,belt),(sign*(width-.04),yb,belt),
                        (sign*(width-.10),min(yb,s['roof_front']),roof-.045),
                        (sign*(width-.10),max(ya,s['roof_rear']),roof-.045)]
            corners=side_corners(s['door_rear'],s['door_front'])
            window('Front door',corners,'driver_glass' if sign>0 else 'passenger_glass','PAINT',group)
            # Triangular fixed quarter panel bridges the raked windshield and door.
            q=[(sign*(width-.04),s['door_front'],belt),
               (sign*(width-.04),s['windshield_base'],belt),
               (sign*(width-.10),s['roof_front'],roof-.045),
               (sign*(width-.10),min(s['door_front'],s['roof_front']),roof-.045)]
            # For the SUV, the two upper points nearly coincide; use a real triangle.
            if abs(q[2][1]-q[3][1])<.008:q=q[:3]
            pane('rear_glass',q)
            for i in range(len(q)):
                if (Vector(q[i])-Vector(q[(i+1)%len(q)])).length>.005:
                    beam('Fixed front quarter pillar',q[i],q[(i+1)%len(q)],.050,'PAINT')
            rear_spans=[(s['roof_rear'],s['door_rear'])] if kind=='suv' else []
            for ya,yb in rear_spans:
                window('Rear side',side_corners(ya,yb),'driver_rear_glass' if sign>0 else 'passenger_rear_glass',
                       'CREAM' if kind=='suv' else 'PAINT')
            # Fixed rail fills the roof/side boundary and carries the rain gutter.
            beam('Roof rain channel',(sign*(width-.09),s['roof_rear']+.06,roof-.051),
                 (sign*(width-.09),s['roof_front']-.04,roof-.051),.034,'CREAM' if kind=='suv' else 'PAINT')
        def panel_seam(y,low,high):
            rows=[]
            for i in range(17):
                z=low+(high-low)*i/16
                row=[]
                for dy in (-.0022,.0022):
                    yy=y+dy;t=max(0,min(1,(z-sill)/(side_top(yy)-sill)))
                    row.append((sign*(side_x(yy,t)+.0018),yy,z))
                rows.append(row)
            skin('Recessed panel shut line',rows,'DARK',thickness=.001)
        for y in (s['door_rear'],s['door_front']):
            panel_seam(y,max(arch_bottom(y)+.018,sill+.02),side_top(y)-.015)
        if kind=='van':panel_seam(-1.14,arch_bottom(-1.14)+.018,belt-.035)
        a,c=xr(width+.013,width+.034,sign)
        hy=s['door_rear']+.10
        if kind=='van':
            box('Recessed vertical door handle',(a,hy,belt-.26),(c,hy+.073,belt-.045),'DARK',group,.028)
            box('Handle grip',(a+.002,hy+.015,belt-.20),(c+.003,hy+.041,belt-.075),'METAL',group,.012)
        else:box('Recessed door handle',(a,hy,belt-.21),(c,hy+.18,belt-.105),'DARK',group,.025)
        # Mirror has a shaped casing, glass face and a stalk attached to the door.
        my=s['door_front']-.18
        tube('Mirror support',(sign*(width-.01),my,belt+.04),
             (sign*(width+.13),my+.035,belt+.14),.018,'DARK',group)
        a,c=xr(width+.075,width+.285,sign)
        mz=belt+.13
        box('Rounded mirror shell',(a,my-.10,mz),(c,my+.10,mz+(.30 if kind=='wrecker' else .17)),'DARK',group,.025)
        box('Mirror reflective face',(a+.012,my-.111,mz+.014),
            (c-.012,my-.098,mz+(.28 if kind=='wrecker' else .154)),'METAL',group,.015)
        if kind=='van':
            beam('Sliding door rail',(sign*(width+.021),s['roof_rear']+.16,belt-.04),
                 (sign*(width+.021),s['door_rear']-.04,belt-.04),.022,'DARK')
            box('Sliding door grip',(min(sign*(width+.01),sign*(width+.031)),-.15,belt-.26),
                (max(sign*(width+.01),sign*(width+.031)),-.079,belt-.045),'DARK',rounding=.025)
    for sign in (-1,1):
        # Flush filler door on the rear quarter.
        fy=rear_axle-.34
        a,c=xr(width+.012,width+.026,sign)
        box('Fuel filler recess',(a,fy-.075,belt-.30),(c,fy+.085,belt-.10),'CLAD',rounding=.025)
        box('Fuel filler flap',(a,fy-.068,belt-.292),(c+(.002 if sign>0 else 0),fy+.078,belt-.108),'PAINT',rounding=.019)
        if kind=='suv':
            for axle in (rear_axle,front_axle):
                for end in (-1,1):
                    y=axle+end*arch
                    rows=[[(sign*(plan_width(y)+.028),y,z),
                           (sign*(plan_width(y)+.018),y+end*.075,z)]
                          for z in (sill+.025,radius+.038)]
                    skin('Fender flare lower leg',rows,'CLAD',thickness=.018)

    # Sculpted seats: inset centre cloth, bolsters and separate padded headrests.
    dash_y=s['steering_fore']+.22
    box('Curved dashboard',(-width+.14,dash_y-.21,belt-.20),(width-.14,dash_y+.075,belt-.018),'CABIN',rounding=.022)
    box('Instrument hood',(.20,dash_y-.26,belt-.045),(.62,dash_y-.07,belt+.09),'DARK',rounding=.014)
    for x in (.29,.45,.54):
        tube('Instrument dial',(x,dash_y-.269,belt+.012),(x,dash_y-.274,belt+.012),.030 if x<.5 else .017,'METAL',segments=20)
    for x in (-.67,-.15,.66):
        box('Dashboard vent',(x-.07,dash_y-.218,belt-.145),(x+.07,dash_y-.210,belt-.075),'DARK',rounding=.01)
    box('Centre console',(-.13,dash_y-.35,s['cab_floor']),(.13,dash_y-.10,belt-.22),'DARK',rounding=.014)
    tube('Gear lever',(0,dash_y-.32,s['cab_floor']+.12),(0,dash_y-.39,s['seat_y']+.10),.018,'DARK')
    box('Gear knob',(-.033,dash_y-.424,s['seat_y']+.085),(.033,dash_y-.357,s['seat_y']+.15),'DARK',rounding=.027)
    seat_rows=[s['seat_fore']]
    if kind=='van':seat_rows += [-.76,-1.73]
    if kind=='suv':seat_rows += [-1.10]
    for row,y in enumerate(seat_rows):
        for sign in (-1,1):
            x=sign*.40
            box('Seat cushion',(x-.23,y-.27,s['seat_y']-.105),(x+.23,y+.25,s['seat_y']+.045),'SEAT',rounding=.028)
            back=box('Shaped seat back',(x-.225,y-.315,s['seat_y']),
                (x+.225,y-.155,s['seat_y']+.59),'SEAT',rounding=.028)
            # Centre cloth and raised side bolsters remain textured front/back.
            box('Seat cloth insert',(x-.145,y-.15,s['seat_y']+.10),
                (x+.145,y-.135,s['seat_y']+.49),'CABIN',rounding=.022)
            for dx in (-.185,.185):
                box('Seat side bolster',(x+dx-.032,y-.16,s['seat_y']+.08),
                    (x+dx+.032,y-.10,s['seat_y']+.51),'SEAT',rounding=.020)
            for dx in (-.075,.075):tube('Headrest post',(x+dx,y-.235,s['seat_y']+.54),
                                       (x+dx,y-.235,s['seat_y']+.66),.010,'METAL')
            box('Rounded headrest',(x-.14,y-.30,s['seat_y']+.62),
                (x+.14,y-.14,s['seat_y']+.78),'SEAT',rounding=.020)
    steering_y=s['steering_fore']
    for i in range(32):
        a,c=2*math.pi*i/32,2*math.pi*(i+1)/32
        p=lambda t:(.41+.155*math.cos(t),steering_y,s['seat_y']+.49+.155*math.sin(t))
        tube('Steering wheel grip',p(a),p(c),.014,'DARK',segments=8)
    tube('Steering column',(.41,steering_y,s['seat_y']+.49),(.41,dash_y,s['seat_y']+.43),.032,'DARK')
    for dx,dz in ((-.13,.02),(.13,.02),(0,-.13)):
        beam('Steering spoke',(.41,steering_y,s['seat_y']+.49),(.41+dx,steering_y,s['seat_y']+.49+dz),.029,'DARK')
    box('Rear hatch handle',(-.23,-length-.059,belt-.20),(.23,-length-.042,belt-.13),'DARK',rounding=.022)
    if kind=='suv':
        # A real dished spare, including a toroidal tire and circular bolt hub.
        spare_y=-length-.19
        spare=lathe(b,'Tailgate spare tire',[(-.10,.25),(-.115,.35),(-.075,.43),(0,.445),(.075,.43),(.115,.35),(.10,.25),(-.10,.25)],'TYRE')
        for v in spare.data.vertices:
            x,y,z=v.co;v.co=(y+.27,spare_y+x,z+1.13)
        keep(spare)
        hub=lathe(b,'Tailgate spare dished rim',[(-.114,.03),(-.116,.23),(-.09,.27),(-.06,.26),(-.065,.08)],'STEEL')
        for v in hub.data.vertices:
            x,y,z=v.co;v.co=(y+.27,spare_y+x,z+1.13)
        keep(hub)
        for i in range(6):
            a=math.tau*i/6;tube('Spare lug',(.27+.085*math.cos(a),spare_y-.125,1.13+.085*math.sin(a)),
                               (.27+.085*math.cos(a),spare_y-.15,1.13+.085*math.sin(a)),.017,'METAL',segments=6)
    if kind=='wrecker':
        box('Service deck',(-width+.08,-length+.17,.76),(width-.08,s['roof_rear']-.035,.86),'CLAD',rounding=.025)
        for sign in (-1,1):
            a,c=xr(width-.36,width-.025,sign)
            for ya,yb in ((-2.52,-1.86),(-1.80,-1.14),(-1.08,.34)):
                # The outer locker is the stamped bed side itself. Its seams
                # follow that surface and rise above the wheel opening.
                aa,cc=xr(width-.36,width-.10,sign)
                box('Locker inner bay',(aa,ya,.84),(cc,yb,1.42),'PAINT',rounding=.012)
                def locker_point(y,z):
                    t=max(0,min(1,(z-sill)/(side_top(y)-sill)))
                    return (sign*(side_x(y,t)+.006),y,z)
                for z in (1.36,):
                    beam('Locker top seam',locker_point(ya+.055,z),locker_point(yb-.055,z),.007,'DARK')
                for y in (ya+.055,yb-.055):
                    low=max(.85,arch_bottom(y)+.035)
                    beam('Locker side seam',locker_point(y,low),locker_point(y,1.36),.006,'DARK')
                for i in range(16):
                    yy=ya+.055+(yb-ya-.11)*i/16
                    yz=ya+.055+(yb-ya-.11)*(i+1)/16
                    beam('Locker lower seam',locker_point(yy,max(.85,arch_bottom(yy)+.035)),
                         locker_point(yz,max(.85,arch_bottom(yz)+.035)),.006,'DARK')
                cy=(ya+yb)*.5;cz=max(1.14,arch_bottom(cy)+.12)
                xx=abs(locker_point(cy,cz)[0]);aa,cc=xr(xx,xx+.014,sign)
                box('Recessed stainless locker latch',(aa,cy-.050,cz-.047),(cc,cy+.05,cz+.047),'METAL',rounding=.009)
            # Upright mud flap behind each rear tire.
            a,c=xr(.70,1.005,sign)
            box('Rear mud flap',(a,rear_axle-arch-.065,.20),(c,rear_axle-arch-.040,.66),'DARK',rounding=.008)
        # Boxed pivot pedestal and deck hardware carry the lifting load.
        box('Boom mounting foot',(-.43,-.19,.855),(.43,.38,.93),'DARK',rounding=.012)
        box('Boom pivot pedestal',(-.22,-.03,.92),(.22,.30,1.46),'PAINT',rounding=.014)
        for sign in (-1,1):
            xx=sign*.24
            mesh('Pedestal gusset',[(xx,-.19,.93),(xx,.35,.93),(xx,.25,1.48)],[(0,1,2)],'PAINT')
            tube('Cab guard upright',(sign*.72,.39,.85),(sign*.72,.39,1.79),.025,'PAINT')
            box('Rear worklamp housing',(sign*.72-.085,.31,1.77),(sign*.72+.085,.40,1.90),'DARK',rounding=.012)
            box('Rear worklamp lens',(sign*.72-.07,.300,1.785),(sign*.72+.07,.309,1.884),'LAMP',rounding=.006)
        for iy in range(23):
            y=-2.39+iy*.115
            for ix in range(10):
                x=-.54+ix*.12
                mesh('Deck tread diamond',[(x-.040,y,.868),(x,y-.014,.870),
                      (x+.040,y,.868),(x,y+.014,.870)],[(0,1,2),(0,2,3)],'METAL')
        beam('Recovery boom outer arm',(0,.22,1.48),(0,-1.35,2.37),.31,'PAINT',depth=.27)
        beam('Recovery boom sliding section',(0,-1.19,2.27),(0,-2.07,2.72),.235,'TOP',depth=.205)
        tube('Hydraulic cylinder',(0,-.11,1.01),(0,-.64,1.61),.085,'PAINT',segments=24)
        tube('Hydraulic chrome piston',(0,-.63,1.60),(0,-1.22,2.20),.048,'METAL',segments=24)
        for y,z,r in ((.19,1.50,.16),(-2.065,2.70,.14)):
            if y>0:tube('Boom pin',(-.20,y,z),(.20,y,z),r,'PAINT',segments=24)
            else:
                tube('Cable sheave',(-.15,y,z),(.15,y,z),r*.92,'DARK',segments=32)
                for sign in (-1,1):tube('Sheave cheek',(sign*.165,y,z),(sign*.20,y,z),r*1.12,'PAINT',segments=24)
            for sign in (-1,1):tube('Boom pivot cap',(sign*.201,y,z),(sign*.224,y,z),r*.38,'METAL',segments=16)
        tube('Winch cable drum',(-.30,.02,1.42),(.30,.02,1.42),.15,'DARK',segments=32)
        for x in (-.31,.31):tube('Winch flange',(x-.015,.02,1.42),(x+.015,.02,1.42),.20,'METAL',segments=32)
        for i in range(18):
            ring=lathe(b,'Wound steel cable',[(i*.028-.24,.145),(i*.028-.234,.157),(i*.028-.223,.157),(i*.028-.217,.145)],'DARK',32)
            for v in ring.data.vertices:v.co.y+=.02;v.co.z+=1.42
            keep(ring)
        tube('Recovery cable',(0,.02,1.59),(0,-2.065,2.81),.011,'DARK')
        tube('Suspended cable',(0,-2.12,2.69),(0,-2.12,1.97),.013,'DARK')
        hook=[]
        for i in range(21):
            t=math.radians(70+285*i/20);hook.append((.115*math.cos(t),-2.12,1.79+.16*math.sin(t)))
        beam('Hook upper shank',(0,-2.12,1.99),hook[0],.06,'METAL')
        for a,c in zip(hook,hook[1:]):tube('Forged hook',a,c,.030,'DARK',segments=10)
        # The bar stops inside the lamp gaskets (x from width-.141): run full
        # width at this height and it hides the brake lamps from behind.
        bar_half=width-.16
        box('Rear recovery bar',(-bar_half,-length-.21,.63),(bar_half,-length-.13,.82),'TOP',rounding=.02)
        for i in range(-3,4):
            x=i*.24-.05
            mesh('Diagonal hazard stripe',[(x-.065,-length-.216,.64),(x+.045,-length-.216,.64),
                (x+.16,-length-.216,.81),(x+.05,-length-.216,.81)],[(0,1,2,3)],'STRIPE')
        box('Lightbar base',(-.52,.94,roof+.014),(.52,1.23,roof+.05),'DARK',rounding=.026)
        for a,c in ((-.50,-.11),(.11,.50)):
            box('Rounded amber beacon',(a,.95,roof+.045),(c,1.22,roof+.185),'AMBER',rounding=.055)
        box('Lightbar centre',(-.105,.96,roof+.045),(.105,1.21,roof+.155),'METAL',rounding=.025)

    for obj in b.objects:
        if obj.name.startswith(('Shaped seat back','Seat cloth insert','Seat side bolster','Rounded headrest','Headrest post')):
            for vertex in obj.data.vertices:
                z=vertex.co.z
                tilt=max(0,min(1,(z-s['seat_y'])/.78))
                vertex.co.y-=.075*tilt
                cx=.40 if vertex.co.x>0 else -.40
                vertex.co.x=cx+(vertex.co.x-cx)*(1-.11*tilt)
            finish(obj)

    if kind in ('van','suv'):
        # Clip the actual stamped triangles, so the molded trim follows the
        # finished curvature instead of sinking through it between vertices.
        trim_top=.59 if kind=='van' else .57
        for source in list(b.objects):
            if 'curved stamping' not in source.name:continue
            sign=1 if source.name.startswith('Left') else -1
            source.data.calc_loop_triangles()
            verts=[];faces=[]
            for triangle in source.data.loop_triangles:
                if triangle.normal.x*sign<.5:continue
                points=[Vector(source.data.vertices[i].co) for i in triangle.vertices]
                clipped=[]
                previous=points[-1];prev_inside=previous.z<=trim_top
                for point in points:
                    inside=point.z<=trim_top
                    if inside!=prev_inside:
                        t=(trim_top-previous.z)/(point.z-previous.z)
                        clipped.append(previous+(point-previous)*t)
                    if inside:clipped.append(point)
                    previous=point;prev_inside=inside
                unique=[]
                for point in clipped:
                    if not any((point-q).length<1e-7 for q in unique):unique.append(point)
                if len(unique)<3:continue
                base=len(verts)
                verts.extend((p.x+sign*.0035,p.y,p.z) for p in unique)
                faces.append(tuple(range(base,base+len(unique))))
            if faces:mesh('Fitted molded lower trim',verts,faces,'CLAD',source.get('vehicle_group','fixed'))

    curved_details=('Recessed grille','Grille horizontal','Grille divider','Headlamp recessed',
                    'Ribbed headlamp','Twin optic','Corner indicator','Bumper vent',
                    'Amber bumper','Lower radiator')
    for obj in b.objects:
        if obj.name.startswith(curved_details):
            for vertex in obj.data.vertices:
                vertex.co.y-=.14*min(1,abs(vertex.co.x)/(width-.02))**8
            finish(obj)

    if kind=='van':
        # A shared plan fillet rounds the roof's four corners, including the
        # adjoining frames, seals and glazing. Deform their joins together so the
        # new curvature cannot pull independently rounded parts apart.
        glass_objects={obj for group in panes.values() for obj in group}
        for obj in b.objects:
            if obj.get('meridian_canopy'):continue
            for vertex in obj.data.vertices:
                x,y,z=vertex.co
                weight=max(0,min(1,(z-(roof-.24))/.16))
                weight=weight*weight*(3-2*weight)
                if weight==0:continue
                corner_radius=.18;cx=width-.10-corner_radius
                cy=s['roof_front']-corner_radius if y>0 else s['roof_rear']+corner_radius
                dx=max(0,abs(x)-cx)
                dy=max(0,y-cy) if y>0 else max(0,cy-y)
                distance=math.hypot(dx,dy)
                if dx>0 and dy>0 and distance>corner_radius:
                    factor=corner_radius/distance
                    vertex.co.x=x-math.copysign(dx*(1-factor)*weight,x)
                    vertex.co.y=y-math.copysign(dy*(1-factor)*weight,y)
                    if 'surface_normal' in obj:
                        # Keep each pane and its surround in their shared plane;
                        # bending only a few corners makes n-gon glass sparkle.
                        normal=Vector(obj['surface_normal'])
                        delta=vertex.co-Vector((x,y,z))
                        vertex.co-=normal*delta.dot(normal)
            finish(obj,smooth=obj not in glass_objects)

    # UV every visible object, including inner skins and the pane backs.
    def uvmap(obj):
        layer=obj.data.uv_layers.new(name='Atlas')
        # Projection bounds are constant for every face of this object.
        all_points=[v.co for v in obj.data.vertices]
        minimum=[min(p[a] for p in all_points) for a in range(3)]
        maximum=[max(p[a] for p in all_points) for a in range(3)]
        for poly in obj.data.polygons:
            name=obj.data.materials[poly.material_index].name
            x0,y0,x1,y1=REGIONS[name]
            points=[obj.data.vertices[obj.data.loops[i].vertex_index].co
                    for i in poly.loop_indices]
            normal=poly.normal
            axes=(1,2) if abs(normal.x)>.60 else (0,2) if abs(normal.y)>.60 else (0,1)
            low=[minimum[a] for a in axes]
            high=[maximum[a] for a in axes]
            if name in ('PAINT','TOP','CREAM','CLAD'):
                low=[(-width,-length,0)[a] for a in axes]
                high=[(width,length,roof+.3)[a] for a in axes]
            for i,p in zip(poly.loop_indices,points):
                q=[max(0,min(1,(p[a]-lo)/(hi-lo))) if hi-lo>1e-8 else .5
                   for a,lo,hi in zip(axes,low,high)]
                layer.data[i].uv=((x0+2+q[0]*(x1-x0-4))/ATLAS,
                                  1-(y1-2-q[1]*(y1-y0-4))/ATLAS)

    for obj in list(b.objects):uvmap(obj)

    def export_group(name,objects):
        assert objects,name
        bpy.ops.object.select_all(action='DESELECT')
        copies=[]
        for obj in objects:
            copy=obj.copy();copy.data=obj.data.copy();bpy.context.scene.collection.objects.link(copy)
            copy.select_set(True);copies.append(copy)
        bpy.context.view_layer.objects.active=copies[0]
        if len(copies)>1:bpy.ops.object.join()
        joined=bpy.context.object
        joined.name='EXPORT_'+name
        result=export_emesh(joined,model/(name+'.emesh'),slug)
        bpy.data.objects.remove(joined,do_unlink=True)
        return result

    report={}
    report['body']=export_group('body',fixed+driver)
    report['body_open']=export_group('body_open',fixed)
    report['driver_door']=export_group('driver_door',driver)
    for name,objects in panes.items():
        if objects:report[name]=export_group(name,objects)

    # Distinct wheels are centered at the origin; game places them on axles.
    # Rear Hookline wheel has twin rubber drums, still one runtime wheel node.
    def wheel_object(name,centres):
        parts=[]
        def add(obj):uvmap(obj);parts.append(obj);return obj
        for cx in centres:
            half=.112 if len(centres)==1 else .078
            profile=[(-half*.77,radius*.60),(-half*.98,radius*.71),
                     (-half,radius*.85),(-half*.81,radius*.96),
                     (-half*.49,radius),(half*.49,radius),
                     (half*.81,radius*.96),(half,radius*.85),
                     (half*.98,radius*.71),(half*.77,radius*.60)]
            add(lathe(b,name+' rounded tire',[(x+cx,r) for x,r in profile],'TYRE',48))
            for sign in (-1,1):
                x=cx+sign*(half+.004)
                rim=[(x-sign*.025,radius*.58),(x,radius*.64),
                     (x+sign*.009,radius*.64),(x+sign*.014,radius*.595),
                     (x-sign*.026,radius*.49),(x-sign*.028,radius*.18),
                     (x+sign*.012,radius*.14),(x+sign*.017,.014)]
                add(lathe(b,name+' dished steel rim',rim,'STEEL',48))
                add(rod(b,name+' hub centre',(x,0,0),(x+sign*.035,0,0),radius*.16,'DARK' if kind=='suv' else 'STEEL',24))
                for i in range(10 if kind=='van' else 8):
                    t=math.tau*i/(10 if kind=='van' else 8)
                    cy=radius*.44*math.cos(t);cz=radius*.44*math.sin(t)
                    add(rod(b,name+' recessed rim vent',(x-sign*.024,cy,cz),
                            (x-sign*.022,cy,cz),radius*.065,'DARK',16))
                for i in range(5 if kind=='van' else 6):
                    t=math.tau*i/(5 if kind=='van' else 6)
                    cy=radius*.23*math.cos(t);cz=radius*.23*math.sin(t)
                    add(rod(b,name+' lug bolt',(x-sign*.02,cy,cz),
                            (x+sign*.009,cy,cz),.015,'METAL',6))
                # Raised rubber sidewall bead, kept matte in its own atlas cell.
                add(lathe(b,name+' sidewall bead',[(x-sign*.005,radius*.75),
                      (x,radius*.758),(x,radius*.772),(x-sign*.005,radius*.78)],'TYRE',48))
            # Small tread grooves change the silhouette and wheel close-up.
            n=40 if kind=='van' else 48
            for i in range(n):
                for shoulder in (-1,1):
                    t=math.tau*i/n+shoulder*.020
                    da=.027 if kind=='van' else .036
                    xa=cx+shoulder*half*.15;xb=cx+shoulder*half*.66
                    verts=[]
                    for rr in (radius-.007,radius+.004):
                        for xx,aa in ((xa,t-da),(xb,t-da+.016),(xb,t+da+.016),(xa,t+da)):
                            verts.append((xx,rr*math.cos(aa),rr*math.sin(aa)))
                    add(b.add_mesh(name+' tread block',verts,[(0,3,2,1),(4,5,6,7),
                        (0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],'TYRE'))
        return parts
    front_wheel=wheel_object('Front wheel',[0])
    rear_wheel=wheel_object('Rear wheel',[-.13,.13] if kind=='wrecker' else [0])
    report['front_wheel']=export_group('front_wheel',front_wheel)
    report['rear_wheel']=export_group('rear_wheel',rear_wheel)
    # Source keeps named construction parts and has four visible wheel copies.
    for obj in front_wheel+rear_wheel:obj.hide_render=True;obj.hide_set(True)
    for axle,wheel_parts in ((front_axle,front_wheel),(rear_axle,rear_wheel)):
        for side in (-1,1):
            for original in wheel_parts:
                copy=original.copy();copy.data=original.data.copy()
                bpy.context.scene.collection.objects.link(copy)
                copy.name=original.name+(' left' if side>0 else ' right')
                copy.location=(side*s['wheel_x'],axle,radius)
                copy.hide_render=False;copy.hide_set(False)
    atlas_image=bpy.data.images.load(str(ROOT/'assets/textures/vehicles'/slug/'body.png'))
    for key,material in b.materials.items():
        material.use_nodes=True
        bsdf=material.node_tree.nodes.get('Principled BSDF')
        node=material.node_tree.nodes.new('ShaderNodeTexImage');node.image=atlas_image
        node.interpolation='Closest'
        material.node_tree.links.new(node.outputs['Color'],bsdf.inputs['Base Color'])
        bsdf.inputs['Roughness'].default_value=.32 if key in ('PAINT','TOP','CREAM') else .84
        if key in ('STEEL','METAL'):
            bsdf.inputs['Metallic'].default_value=.65
            bsdf.inputs['Roughness'].default_value=.32
        if key in ('PAINT','TOP','CREAM'):
            bsdf.inputs['Coat Weight'].default_value=.16
        if key in ('LAMP','AMBER','RED'):
            bsdf.inputs['Roughness'].default_value=.24
            bsdf.inputs['Coat Weight'].default_value=.30
        if key=='WINDOW':
            material.node_tree.links.remove(bsdf.inputs['Base Color'].links[0])
            bsdf.inputs['Base Color'].default_value=(.68,.81,.85,1)
            bsdf.inputs['Transmission Weight'].default_value=.94
            bsdf.inputs['Roughness'].default_value=.1
        if key=='TYRE':bsdf.inputs['Specular IOR Level'].default_value=0
    atlas_image.pack()
    bpy.ops.wm.save_as_mainfile(filepath=str(model/'source.blend'))
    (ROOT/'build'/f'{slug}-fit-report.json').write_text(json.dumps({
        'identity':s['brand']+' '+s['model'],'shape':s,'parts':report,
        'source':str(model/'source.blend')},indent=2)+'\n')
    print('CANDIDATE_1991',slug,json.dumps(report))


if __name__=='__main__':
    args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
    build(args[0])
