"""Inset front fascia and bumper furniture for the BWC 360.

Construction axes: X left, Y front, Z up. All depth offsets are measured
from the adjacent stamping, before the common plan-view corner deformation.
"""
import math
import bpy,bmesh


def nose_y(h):return 2.217-.105*(h-.600)

BUMPER_PROFILE=[(.296,-.018),(.312,.004),(.350,.023),(.480,.037),(.566,.025),(.610,.016)]

def bumper_y(h):
    for (a,u),(b,v) in zip(BUMPER_PROFILE,BUMPER_PROFILE[1:]):
        if h<=b:return 2.20+u+(v-u)*max(0,(h-a)/(b-a))
    return 2.20+BUMPER_PROFILE[-1][1]

def rounded(cx,h,w,t,r=.025,segments=5,taper=0):
    points=[]
    for x,z,angle in [(w/2-r,t/2-r,0),(-w/2+r,t/2-r,90),(-w/2+r,-t/2+r,180),(w/2-r,-t/2+r,270)]:
        for i in range(segments+1):
            a=math.radians(angle+i*90/segments);zz=z+r*math.sin(a)
            points.append((cx+(x+r*math.cos(a))*(1+taper*zz/(t/2)),h+zz))
    return points

def soften(obj,angle=.60):
    # Smooth shallow changes, keep actual crease/return edges sharp.
    bm=bmesh.new();bm.from_mesh(obj.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    for f in bm.faces:f.smooth=True
    for e in bm.edges:e.smooth=len(e.link_faces)==2 and e.calc_face_angle()<angle
    bm.to_mesh(obj.data);bm.free();obj.data.update()
    return obj


def build_front(mesh,material,bevel,fixed):
    def volume(name,outline,surface,key,depth=.01,offset=0):
        n=len(outline);vs=[(x,surface(h)+offset,h) for x,h in outline]+[(x,surface(h)+offset-depth,h) for x,h in outline]
        fs=[tuple(range(n)),tuple(reversed(range(n,2*n)))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
        return mesh(name,vs,fs,key)

    def punch(target,outline,surface):
        cutter=volume('Temporary aperture cutter',outline,surface,'BLACK',.25,.08)
        fixed.remove(cutter)
        bpy.context.view_layer.objects.active=target
        mod=target.modifiers.new('Actual inset aperture','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
        bpy.ops.object.modifier_apply(modifier=mod.name)
        bpy.data.objects.remove(cutter,do_unlink=True)

    def inset(name,loops,surface,keys,back='BLACK'):
        """Connected radius/return strips; no solid cap underneath the opening."""
        n=len(loops[0][0]);vs=[(x,surface(h)+d,h) for points,d in loops for x,h in points];fs=[];mats=[]
        for j,key in enumerate(keys):
            for i in range(n):fs.append((j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i));mats.append(key)
        if back:fs.append(tuple((len(loops)-1)*n+i for i in range(n)));mats.append(back)
        obj=mesh(name,vs,fs,keys[0]);unique=list(dict.fromkeys(mats));obj.data.materials.clear()
        for key in unique:obj.data.materials.append(material(key))
        for f,key in zip(obj.data.polygons,mats):f.material_index=unique.index(key)
        # Keep the front-facing open bowl orientation after normal propagation.
        bm=bmesh.new();bm.from_mesh(obj.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        if back:
            floor=bm.faces[-1]
            if floor.normal.y<0:bmesh.ops.reverse_faces(bm,faces=list(bm.faces))
        bm.to_mesh(obj.data);bm.free();soften(obj)
        return obj

    # This silhouette follows the existing hood/fender front section.
    outline=[(-.833,.585),(.833,.585),(.833,.749),(.815,.785),(.777,.809),(.750,.824),(.650,.816),(.580,.826),(.380,.838),(.340,.842),(0,.849),(-.340,.842),(-.380,.838),(-.580,.826),(-.650,.816),(-.750,.824),(-.777,.809),(-.815,.785),(-.833,.749)]
    nose=volume('Continuous recessed nose stamping',outline,nose_y,'PAINT',.092)
    # Tiny overlap at the outer boundary closes the fixed panel join.
    for s in (-1,1):
        cx=s*.172;h=.700
        aperture=rounded(cx,h,.310,.198,.039,taper=.055)
        punch(nose,aperture,nose_y)
        profiles=[(aperture,0),
                  (rounded(cx,h,.300,.188,.035,taper=.055),-.002),
                  (rounded(cx,h,.294,.182,.033,taper=.055),-.001),
                  (rounded(cx,h,.282,.170,.030,taper=.055),-.006),
                  (rounded(cx,h,.280,.168,.029,taper=.055),-.030)]
        inset('Recessed BWC grille and rolled rim',profiles,nose_y,['PAINT','METAL','METAL','BLACK'])
        for off in [-.108,-.081,-.054,-.027,0,.027,.054,.081,.108]:
            height=.119 if abs(off)>.085 else .145
            blade=rounded(cx+off,h,.005,height,.002,segments=1)
            soften(volume('Inset rounded grille vane',blade,nose_y,'METAL',.006,-.017))

        # Rounded swept lamp aperture, completely beneath the hood's brow.
        lamp=[(s*x,z) for x,z in [(.366,.663),(.385,.653),(.470,.651),(.535,.660),(.710,.659),(.795,.681),(.820,.712),(.805,.759),(.769,.789),(.715,.799),(.521,.808),(.445,.797),(.401,.766)]]
        # Corner cutting gives the formerly sharp polygon a modest pressed radius.
        curve=[]
        for i,p in enumerate(lamp):
            prev=lamp[(i-1)%len(lamp)];nxt=lamp[(i+1)%len(lamp)]
            a=(p[0]*.83+prev[0]*.17,p[1]*.83+prev[1]*.17);c=(p[0]*.83+nxt[0]*.17,p[1]*.83+nxt[1]*.17)
            curve.extend([a,c])
        punch(nose,curve,nose_y)
        for obj in list(fixed):
            if obj.name.startswith('Front integrated quarter'):
                center=sum(v.co.x for v in obj.data.vertices)/len(obj.data.vertices)
                if center*s>0:punch(obj,curve,nose_y)
        cx=s*.593;cz=.728
        shrink=lambda k:[(cx+(x-cx)*k,cz+(h-cz)*k) for x,h in curve]
        inset('Flush headlamp pocket',[(curve,0),(shrink(.976),-.002),(shrink(.961),-.009),(shrink(.953),-.030)],nose_y,['PAINT','BLACK','METAL'],back='METAL')
        for x,r in [(s*.486,.059),(s*.640,.061)]:
            circle=lambda rr:[(x+rr*math.cos(i*math.tau/24),.729+rr*math.sin(i*math.tau/24)) for i in range(24)]
            inset('Sunken round optical lens',[(circle(r),-.012),(circle(r*.91),-.010),(circle(r*.84),-.021),(circle(r*.68),-.014)],nose_y,['LAMP','BLACK','METAL'],back='LAMP')
        amber=[(s*x,h) for x,h in [(.723,.670),(.794,.690),(.812,.714),(.794,.758),(.772,.779)]]
        soften(volume('Inset amber corner lens',amber,nose_y,'AMBER',.004,-.010))
    # No global bevel: aperture radii come from the connected strips above.
    soften(nose)

    bumpers=[]
    columns=[(-.853,-.30),(-.866,-.18)]+[(x,0) for x in [-.858,-.835,-.79,-.68,-.42,0,.42,.68,.79,.835,.858]]+[(.866,-.18),(.853,-.30)]
    for rear in (False,True):
        vs=[]
        for h,d in BUMPER_PROFILE:
            for x,dy in columns:
                y=2.20+d+dy;vs.append((x,-y-.028 if rear else y,h))
        n=len(columns);fs=[]
        for j in range(len(BUMPER_PROFILE)-1):
            for i in range(n-1):fs.append((j*n+i,j*n+i+1,(j+1)*n+i+1,(j+1)*n+i))
        if not rear:fs=[tuple(reversed(f)) for f in fs]
        obj=mesh('Blended rear bumper' if rear else 'Blended front bumper',vs,fs,'PAINT')
        bpy.context.view_layer.objects.active=obj;mod=obj.modifiers.new('Bumper inside return','SOLIDIFY');mod.thickness=.025;mod.offset=-1;bpy.ops.object.modifier_apply(modifier=mod.name)
        soften(obj);bumpers.append(obj)
        # A 2 mm raised molding follows the same section at every height.
        strip=[]
        for h in [.549,.579]:
            for x,dy in columns:
                y=bumper_y(h)+dy+.002;strip.append((x,-y-.028 if rear else y,h))
        fs=[(i,i+1,n+i+1,n+i) for i in range(n-1)]
        soften(mesh('Conforming impact molding',strip,fs if rear else [tuple(reversed(f)) for f in fs],'BLACK'))
    for s in (-1,1):
        shape=rounded(s*.577,.421,.211,.072,.030)
        punch(bumpers[0],shape,bumper_y)
        inset('Recessed fog lamp',[(shape,0),(rounded(s*.577,.421,.198,.061,.026),-.002),(rounded(s*.577,.421,.180,.048,.022),-.020)],bumper_y,['PAINT','BLACK'],back='LAMP')
        shape=rounded(s*.550,.337,.255,.036,.015)
        punch(bumpers[0],shape,bumper_y)
        inset('Recessed side air inlet',[(shape,0),(rounded(s*.550,.337,.244,.026,.011),-.004),(rounded(s*.550,.337,.240,.024,.010),-.021)],bumper_y,['PAINT','BLACK'])
    shape=rounded(0,.337,.608,.061,.021)
    punch(bumpers[0],shape,bumper_y)
    inset('Recessed center air inlet',[(shape,0),(rounded(0,.337,.596,.050,.017),-.004),(rounded(0,.337,.592,.046,.015),-.025)],bumper_y,['PAINT','BLACK'])
    for obj in bumpers:soften(obj)
