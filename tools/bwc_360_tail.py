"""BWC rear stamping, inset lamps and a plate pocket fitted to the body.

Construction axes X left, Y forward, Z up. Transverse stations must be
present BEFORE the shared plan-view warp; a wide ngon cap collapses inward.
"""
import math
import bpy,bmesh
from bwc_360_fascia import rounded,bumper_y,soften
from bwc_360_spec import PLATE_MOUNTS


def tail_y(h):
    return -(bumper_y(h)+.028) if h<.610 else -2.244+(h-.610)*(.014/.245)


def transverse(obj,heights=(),columns=()):
    bm=bmesh.new();bm.from_mesh(obj.data)
    for axis,values in [(0,sorted(set([i*.06 for i in range(-15,16)]+list(columns)))),(2,heights)]:
        for value in values:
            co=[0,0,0];co[axis]=value;n=[0,0,0];n[axis]=1
            bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=1e-7,plane_co=co,plane_no=n)
    bm.to_mesh(obj.data);bm.free();obj.data.update()
    return soften(obj)


def build_tail(mesh,material,fixed):
    def volume(name,outline,surface,key,depth=.035,outset=0):
        n=len(outline);vs=[(x,surface(h)-outset,h) for x,h in outline]+[(x,surface(h)+depth,h) for x,h in outline]
        fs=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
        obj=mesh(name,vs,fs,key)
        bm=bmesh.new();bm.from_mesh(obj.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(obj.data);bm.free()
        return obj
    def punch(target,outline):
        cutter=volume('Temporary rear aperture',outline,tail_y,'BLACK',.17,.07);fixed.remove(cutter)
        bpy.context.view_layer.objects.active=target
        mod=target.modifiers.new('Fitted rear opening','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
        bpy.ops.object.modifier_apply(modifier=mod.name);bpy.data.objects.remove(cutter,do_unlink=True)
    def pocket(name,loops,keys,back):
        # Add shared stations where the bumper profile changes pitch.
        # Otherwise the tall plate border bridges the curve and stands proud.
        outlines=[[] for _ in loops]
        outer=loops[0][0]
        for i,a in enumerate(outer):
            c=outer[(i+1)%len(outer)];cuts=[0.]
            if abs(c[1]-a[1])>1e-8:
                cuts += [(h-a[1])/(c[1]-a[1]) for h in [.566,.610] if 1e-6<(h-a[1])/(c[1]-a[1])<1-1e-6]
            for j,(outline,_) in enumerate(loops):
                p=outline[i];q=outline[(i+1)%len(outline)]
                outlines[j].extend([(p[0]+t*(q[0]-p[0]),p[1]+t*(q[1]-p[1])) for t in sorted(cuts)])
        loops=[(outline,pair[1]) for outline,pair in zip(outlines,loops)]
        n=len(loops[0][0]);vs=[(x,surface(h),h) for outline,surface in loops for x,h in outline];fs=[];mats=[]
        for j,key in enumerate(keys):
            for i in range(n):fs.append((j*n+i,j*n+(i+1)%n,(j+1)*n+(i+1)%n,(j+1)*n+i));mats.append(key)
        fs.append(tuple((len(loops)-1)*n+i for i in range(n)));mats.append(back)
        obj=mesh(name,vs,fs,keys[0]);unique=list(dict.fromkeys(mats));obj.data.materials.clear()
        for key in unique:obj.data.materials.append(material(key))
        for face,key in zip(obj.data.polygons,mats):face.material_index=unique.index(key)
        bm=bmesh.new();bm.from_mesh(obj.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        if bm.faces[-1].normal.y>0:bmesh.ops.reverse_faces(bm,faces=list(bm.faces))
        bm.to_mesh(obj.data);bm.free();return obj
    # Upper boundary follows the trunk crown and the quarter's rolled shoulder.
    outline=[(-.826,.608),(.826,.608),(.826,.774),(.809,.815),(.772,.838),(.750,.853),(.650,.845),(.580,.855),(.380,.867),(.340,.871),(0,.878),(-.340,.871),(-.380,.867),(-.580,.855),(-.650,.845),(-.750,.853),(-.772,.838),(-.809,.815),(-.826,.774)]
    stamping=volume('Continuous fitted rear stamping',outline,tail_y,'PAINT',.045)
    for side in (-1,1):
        cx=side*.633;h=.730
        def lens_shape(w,t,r):
            pts=rounded(cx,h,w,t,r,segments=3)
            return [(x-side*.030*max(0,(z-.770)/.060)*max(0,(abs(x)-.700)/.124),z) for x,z in pts]
        shape=lens_shape(.382,.190,.022)
        punch(stamping,shape)
        for obj in list(fixed):
            if obj.name.startswith('Rear integrated quarter') and sum(v.co.x for v in obj.data.vertices)*side>0:punch(obj,shape)
        lamp=pocket('Flush combined rear lens',[(shape,tail_y),
            (lens_shape(.378,.186,.020),lambda z:tail_y(z)+.001),
            (lens_shape(.373,.181,.018),lambda z:tail_y(z)+.0025),
            (lens_shape(.370,.178,.017),lambda z:tail_y(z)+.004)],['PAINT','BLACK','RED'],'RED')
        transverse(lamp,heights=[.690,.730,.749,.803],columns=[side*x for x in [.446,.670,.718,.800]])
        slots={m.name:i for i,m in enumerate(lamp.data.materials)}
        for key in ('LAMP','AMBER'):
            slots[key]=len(lamp.data.materials);lamp.data.materials.append(material(key))
        for face in lamp.data.polygons:
            if lamp.data.materials[face.material_index].name!='RED':continue
            c=face.center;x=abs(c.x)
            if .446<x<.670 and .690<c.z<.730:face.material_index=slots['LAMP']
            elif .718<x<.800 and .749<c.z<.803:face.material_index=slots['AMBER']
    # A real opening crosses the upper bumper, with the backing inside it.
    shape=rounded(0,.650,.350,.192,.013,segments=3)
    targets=[stamping]+[o for o in fixed if o.name=='Blended rear bumper' or
        (o.name.startswith('Conforming impact molding') and sum(v.co.y for v in o.data.vertices)<0)]
    for obj in targets:punch(obj,shape)
    floor=PLATE_MOUNTS['rear']['center'][2]+.004
    mount=pocket('Inset rear plate pocket',[(shape,tail_y),
        (rounded(0,.650,.343,.185,.011,segments=3),lambda h:tail_y(h)+.002),
        (rounded(0,.650,.326,.174,.008,segments=3),lambda h:floor-.001),
        (rounded(0,.650,.322,.170,.007,segments=3),lambda h:floor)],['PAINT','PAINT','BLACK'],'BLACK')
    for obj in [stamping,mount]+targets[1:]:transverse(obj)
    # Refine newly cut quarter cap edges before the same export deformation.
    for obj in fixed:
        if obj.name.startswith('Rear integrated quarter'):soften(obj)
