"""Meridian canopy traced from its selected concept and directional references.

The roof, end screens and side apertures share the same boundary functions.
Blender axes: X across, Y forward, Z up; dimensions in metres.
"""
import math
from mathutils import Vector
from candidate_1991_geometry import finish


def build_cabin(b, s, keep, mesh, skin, panes):
    belt=s['belt']; rear=s['roof_rear']; front=s['roof_front']
    rear_base=-s['half_length']+.04; front_base=s['windshield_base']
    made_before=set(b.objects)

    def eave(y):
        # Shallow pressed roof. The windows follow the end curves, so there is
        # no thick roof cap above a straight, shortened window opening.
        back=max(0,1-(y-rear)/.12)
        nose=max(0,1-(front-y)/.26)
        return 1.790-.065*back*back-.065*nose*nose

    def side_x(z):
        # Front/rear references put the upper glass at ~80% of belt width.
        return .89-.175*(z-belt)/(1.805-belt)

    def roof_point(u,y):
        t=(y-rear)/(front-rear)
        # A rolled transverse shoulder meets the inclined window sides. A
        # parabola here creates a square roof-to-pillar corner in front view.
        crown=(.055+.010*math.sin(math.pi*t))*math.sqrt(max(0,1-u**8))
        bow=(.040*t-.030*(1-t))*(1-u*u)
        return (side_x(eave(y))*u,y+bow,eave(y)+crown)

    ys=sorted(set([rear+(front-rear)*j/14 for j in range(15)]+
                  [rear+.12*j/8 for j in range(9)]+
                  [front-.26*j/10 for j in range(11)]+[-1.14,s['door_rear']]))
    us=[-math.cos(math.pi*i/20) for i in range(21)]
    rows=[[roof_point(u,y) for u in us] for y in ys]
    skin('Meridian shared canopy',rows,'PAINT',thickness=.025)
    skin('Meridian fitted headliner',[[tuple(Vector(p)+Vector((0,0,-.037)))
          for p in row] for row in rows],'HEADLINER',thickness=.009)

    def area(poly):
        return sum(p[0]*q[1]-q[0]*p[1] for p,q in zip(poly,poly[1:]+poly[:1]))/2

    def offset(poly,d):
        # Convex polygon inset, retaining the reference's unequal corner radii.
        out=[]
        for i,p in enumerate(poly):
            p=Vector(p);a=Vector(poly[i-1]);c=Vector(poly[(i+1)%len(poly)])
            ab=(p-a).normalized();bc=(c-p).normalized()
            n1=Vector((-ab.y,ab.x));n2=Vector((-bc.y,bc.x))
            if isinstance(d,(tuple,list)):
                det=n1.x*n2.y-n1.y*n2.x
                move=Vector(((d[i-1]*n2.y-d[i]*n1.y)/det,
                             (n1.x*d[i]-n2.x*d[i-1])/det))
            else:
                bis=n1+n2;move=bis*(d/max(.05,bis.dot(n1)))
            out.append(tuple(p+move))
        return out

    def soften(poly,radii):
        loop=[]
        for i,point in enumerate(poly):
            p=Vector(point);a=Vector(poly[i-1]);c=Vector(poly[(i+1)%len(poly)])
            r=min(radii[i],(p-a).length*.42,(c-p).length*.42)
            start=p+(a-p).normalized()*r;end=p+(c-p).normalized()*r
            for j in range(7):
                t=j/6
                loop.append(tuple((1-t)**2*start+2*t*(1-t)*p+t*t*end))
        return loop

    def ray_hit(poly,centre,direction):
        # Matching radial samples connect a curved aperture to its full panel,
        # preserving the exact outer roof/pillar boundaries without booleans.
        cross=lambda a,c:a.x*c.y-a.y*c.x
        hits=[]
        for a,c in zip(poly,poly[1:]+poly[:1]):
            a=Vector(a);edge=Vector(c)-a;den=cross(direction,edge)
            if abs(den)<1e-10:continue
            t=cross(a-centre,edge)/den;q=cross(a-centre,direction)/den
            if t>0 and -.0001<=q<=1.0001:hits.append(t)
        assert hits,(poly,centre,direction)
        return tuple(centre+min(hits)*direction)

    def ring(name,outer,inner,material,normal,group='fixed',depth=.018):
        n=len(outer);normal=Vector(normal).normalized()*depth
        verts=outer+inner+[tuple(Vector(p)-normal) for p in outer+inner]
        faces=[]
        for i in range(n):
            j=(i+1)%n
            faces.extend(((i,j,n+j,n+i),(2*n+i,3*n+i,3*n+j,2*n+j),
                          (i,2*n+i,2*n+j,j),(n+i,n+j,3*n+j,3*n+i)))
        return mesh(name,verts,faces,material,group)

    def window(name,outer2,shape2,radii,surface,normal,category,group='fixed',border=.025,trim_inset=None):
        assert area(outer2)>0 and area(shape2)>0
        centre=sum((Vector(p) for p in shape2),Vector((0,0)))/len(shape2)
        # Include every outer station so the side meets every roof cross section.
        aperture=soften(offset(shape2,trim_inset if trim_inset else border),
                        [r*.65 for r in radii] if trim_inset else radii)
        clear=soften(offset(shape2,border+.030),[max(.012,r-.016) for r in radii])
        angles=[math.tau*i/8 for i in range(8)]
        angles+= [math.atan2(p[1]-centre.y,p[0]-centre.x)%math.tau for p in outer2+clear]
        angles=sorted(set(round(t,5) for t in angles))
        directions=[Vector((math.cos(t),math.sin(t))) for t in angles]
        outer=[ray_hit(outer2,centre,d) for d in directions]
        seal=[ray_hit(aperture,centre,d) for d in directions]
        inside=[ray_hit(clear,centre,d) for d in directions]
        ring(name+' painted stamping',[surface(*p) for p in outer],
             [surface(*p) for p in seal],'PAINT',normal,group)
        # Black ceramic border and gasket follow the same surface as the pane.
        ring(name+' black window surround',[surface(*p) for p in seal],
             [surface(*p) for p in inside],'DARK',normal,group,.010)
        # Tessellated curved glass, not a non-planar n-gon. Inset the glass 3 mm.
        verts=[];faces=[];n=len(inside);normal=Vector(normal).normalized()
        verts.append(tuple(Vector(surface(*centre))-normal*.003))
        for row in range(1,7):
            for p in inside:
                uv=centre+(Vector(p)-centre)*row/6
                verts.append(tuple(Vector(surface(*uv))-normal*.003))
        for i in range(n):faces.append((0,1+i,1+(i+1)%n))
        for row in range(5):
            a=1+row*n;c=a+n
            for i in range(n):
                j=(i+1)%n;faces.append((a+i,c+i,c+j,a+j))
        obj=finish(b.add_mesh(name+' glass',verts,faces,'WINDOW'))
        obj['vehicle_group']=group;panes[category].append(obj)

    for end,base,category,normal,label in (
            (front,front_base,'windshield',(0,1,1),'Meridian windshield'),
            (rear,rear_base,'rear_glass',(0,-1,0),'Meridian hatch')):
        def surface(u,v,end=end,base=base):
            q=2*u-1;top=Vector(roof_point(q,end))
            bottom=Vector((.89*q,base,belt+(.085*(1-q**4) if end==front else 0)))
            p=bottom*(1-v)+top*v
            p.y+=(1 if end==front else -1)*.045*(1-q*q)*math.sin(math.pi*v)
            return tuple(p)
        # Header has a shallow transverse arch. Screen edges taper with the cab.
        quad=[(0,0),(1,0),(1,1),(0,1)]
        window(label,quad,quad,[.035,.035,.105,.105],surface,normal,category,border=.018)

    # Side windows are on one inclined plane and inherit the roof boundary.
    for sign in (-1,1):
        def side(y,z):return (sign*side_x(z),y,z)
        normal=(sign,0,.263)
        spans=[(rear_base,-1.14,'Rear quarter','driver_rear_glass' if sign>0 else 'passenger_rear_glass','fixed'),
               (-1.14,s['door_rear'],'Sliding door','driver_rear_glass' if sign>0 else 'passenger_rear_glass','fixed'),
               (s['door_rear'],s['door_front'],'Front door','driver_glass' if sign>0 else 'passenger_glass','driver' if sign>0 else 'fixed')]
        for a,c,label,category,group in spans:
            top_a=max(a,rear);top_c=min(c,front)
            stations=sorted(set([top_a,top_c]+[y for y in ys if top_a<y<top_c]))
            outer=[(a,belt),(c,belt)]
            if c>front:
                corner_z=belt+(eave(front)-belt)*(front_base-c)/(front_base-front)
                outer.append((c,corner_z))
            outer += [(y,eave(y)) for y in reversed(stations)]
            # The rear quarter leans forward, and the front window has a long
            # rounded top corner; central glazing keeps straighter divisions.
            if a<rear:
                shape=[(a+.075,belt),(c,belt),(c,eave(c)),(rear+.12,eave(rear+.12))]
                radii=[.035,.035,.045,.17]
                trim=(.018,.001,.022,.060)
            elif c>front:
                shape=[(a,belt),(c,belt),(c,corner_z),(front-.04,eave(front-.04)),(a,eave(a))]
                radii=[.025,.030,.045,.22,.035]
                trim=(.018,.025,.024,.020,.001)
            else:
                shape=[(a,belt),(c,belt),(c,eave(c)),(a,eave(a))]
                radii=[.035,.035,.050,.050]
                trim=(.018,.001,.022,.001)
            window('Meridian '+label,outer,shape,radii,side,normal,category,group,
                   border=.028 if c>front else .036,trim_inset=trim)
        # Small fixed triangle ahead of the opening front door, fitted to the
        # windshield edge. It shares the same side surface and slope.
        a=s['door_front'];z=belt+(eave(front)-belt)*(front_base-a)/(front_base-front)
        triangle=[(a,belt),(front_base,belt),(a,z)]
        window('Meridian fixed quarter',triangle,triangle,[.018,.018,.018],side,
               normal,'rear_glass',border=.019)

    for obj in set(b.objects)-made_before:
        obj['meridian_canopy']=True
