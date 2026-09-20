"""Shared BWC flank contour and true quarter-to-door boundaries.

Construction axes X left, Y forward, Z up. The perimeter uses a 6 mm
shut line; shallow panel curvature is authored before a 1.5 mm edge radius.
"""
import math
import bpy,bmesh
from mathutils import Vector
from mathutils.bvhtree import BVHTree

BODY_LEVELS=[(.230,.791),(.259,.799),(.265,.801),(.31,.822),(.40,.840),
             (.55,.854),(.67,.858),(.76,.853),(.82,.848),(.875,.842),(.90,.842)]

def side_x(h):
    if h>.90:return .842-(h-.90)*.27
    for (a,x),(b,y) in zip(BODY_LEVELS,BODY_LEVELS[1:]):
        if h<=b:return x+(y-x)*max(0,(h-a)/(b-a))
    return BODY_LEVELS[-1][1]

def _rear_curve(h):
    return max(-1.122,-1.30+math.sqrt(max(0,.414**2-(h-.325)**2))+.010) if h<.739 else -1.122

def rear_edge(h):
    # The fixed cut follows the actual piecewise door boundary, including bends.
    for (a,_),(b,_) in zip(BODY_LEVELS,BODY_LEVELS[1:]):
        if h<=b:return _rear_curve(a)+(_rear_curve(b)-_rear_curve(a))*max(0,(h-a)/(b-a))
    return -1.122

def conform_quarter(obj,s,front):
    if not front:
        # Remove the actual quarter overlap instead of burying the door in it.
        heights=sorted(set([.20]+[h for h,x in BODY_LEVELS]+[.325,.60,.70,.715,.730,.739,1.0]))
        outline=[(rear_edge(h)-.006,h) for h in heights]+[(.10,1.0),(.10,.20)]
        n=len(outline);vs=[(x,y,h) for x in [-1.3,1.3] for y,h in outline]
        fs=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
        data=bpy.data.meshes.new('Quarter door aperture cutter');data.from_pydata(vs,[],fs);data.update()
        cutter=bpy.data.objects.new(data.name,data);bpy.context.scene.collection.objects.link(cutter)
        bm=bmesh.new();bm.from_mesh(data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(data);bm.free()
        bpy.context.view_layer.objects.active=obj
        mod=obj.modifiers.new('True rear door aperture','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
        bpy.ops.object.modifier_apply(modifier=mod.name);bpy.data.objects.remove(cutter,do_unlink=True)
    bm=bmesh.new();bm.from_mesh(obj.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    original=BVHTree.FromBMesh(bm)
    # Split only the part near the door so its height curve can meet the skin.
    for h,x in BODY_LEVELS[2:]:
        near=[f for f in bm.faces if any((v.co.y<1.10 if front else v.co.y>-1.40) for v in f.verts)]
        geom=list(set(near+[e for f in near for e in f.edges]+[v for f in near for v in f.verts]))
        bmesh.ops.bisect_plane(bm,geom=geom,dist=1e-7,plane_co=(0,0,h),plane_no=(0,0,1))
    for y in ([.94,1.02,1.10] if front else [-1.40,-1.30,-1.20,-1.10,-1.00]):
        bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=1e-7,plane_co=(0,y,0),plane_no=(0,1,0))
    for v in bm.verts:
        x,y,h=v.co
        gap=y-.864 if front else rear_edge(h)-.006-y
        w=max(0,min(1,1-gap/.24));w=w*w*(3-2*w)
        w*=max(0,min(1,(.940-h)/.040))
        if w==0 or abs(x)<.76:continue
        hit=original.ray_cast(Vector((s*2,y,h)),Vector((-s,0,0)))[0]
        delta=side_x(h)-(abs(hit.x) if hit is not None else abs(x))
        v.co.x+=s*delta*w*max(0,min(1,(abs(x)-.76)/.045))
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bmesh.ops.dissolve_limit(bm,angle_limit=.002,verts=list(bm.verts),edges=list(bm.edges),delimit={'MATERIAL'})
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    for f in bm.faces:f.smooth=True
    for e in bm.edges:e.smooth=len(e.link_faces)==2 and e.calc_face_angle()<.60
    bm.to_mesh(obj.data);bm.free();obj.data.update()
