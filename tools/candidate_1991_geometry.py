"""Surface primitives for the refined 1991 fleet (Blender coordinates)."""
import math
import bpy
import bmesh
from mathutils import Vector


def finish(obj, smooth=True):
    bm=bmesh.new();bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.000001)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    for f in bm.faces:f.smooth=smooth
    for e in bm.edges:
        e.smooth=len(e.link_faces)==2 and e.calc_face_angle(0)<.65
    bm.to_mesh(obj.data);bm.free();obj.data.update()
    return obj


def bevel(obj, amount=.025, segments=3):
    bpy.context.view_layer.objects.active=obj
    mod=obj.modifiers.new('Machined edge radius','BEVEL')
    mod.width=amount;mod.segments=segments;mod.affect='EDGES'
    bpy.ops.object.modifier_apply(modifier=mod.name)
    return finish(obj)


def sheet(builder,name,rows,material,thickness=.022):
    n=len(rows[0]);verts=[p for row in rows for p in row]
    faces=[(j*n+i,j*n+i+1,(j+1)*n+i+1,(j+1)*n+i)
           for j in range(len(rows)-1) for i in range(n-1)]
    obj=builder.add_mesh(name,verts,faces,material)
    finish(obj)
    if thickness:
        bpy.context.view_layer.objects.active=obj
        mod=obj.modifiers.new('Panel inward return','SOLIDIFY')
        mod.thickness=thickness;mod.offset=-1
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return finish(obj)


def rounded_loop(corners,inset=0,rounding=.08,steps=6):
    # corners: lower left, lower right, upper right, upper left.
    a,b,c,d=map(Vector,corners)
    def at(u,v):return tuple(a*(1-u)*(1-v)+b*u*(1-v)+c*u*v+d*(1-u)*v)
    edge=inset;r=rounding
    out=[]
    for cx,cy,start in ((1-edge-r,edge+r,-90),(1-edge-r,1-edge-r,0),
                        (edge+r,1-edge-r,90),(edge+r,edge+r,180)):
        for i in range(steps+1):
            t=math.radians(start+90*i/steps)
            out.append(at(cx+r*math.cos(t),cy+r*math.sin(t)))
    return out


def surround(builder,name,corners,material,inner=.035,depth=.028,rounding=.075):
    outer=rounded_loop(corners,0,rounding)
    inside=rounded_loop(corners,inner,max(.03,rounding-inner*.4))
    normal=(Vector(corners[1])-Vector(corners[0])).cross(Vector(corners[2])-Vector(corners[0])).normalized()
    n=len(outer)
    verts=outer+inside+[tuple(Vector(p)-normal*depth) for p in outer+inside]
    faces=[]
    for j in range(n):
        k=(j+1)%n
        faces.extend(((j,k,n+k,n+j),(2*n+j,3*n+j,3*n+k,2*n+k),
                      (j,2*n+j,2*n+k,k),(n+j,n+k,3*n+k,3*n+j)))
    return finish(builder.add_mesh(name,verts,faces,material)),inside


def rod(builder,name,start,end,radius,material,segments=16,radius_end=None):
    a,b=Vector(start),Vector(end);axis=(b-a).normalized()
    u=axis.cross(Vector((0,0,1)))
    if u.length<.001:u=axis.cross(Vector((0,1,0)))
    u.normalize();v=axis.cross(u).normalized()
    verts=[]
    for p,r in ((a,radius),(b,radius if radius_end is None else radius_end)):
        for i in range(segments):
            t=2*math.pi*i/segments
            verts.append(tuple(p+r*(u*math.cos(t)+v*math.sin(t))))
    faces=[(i,(i+1)%segments,(i+1)%segments+segments,i+segments) for i in range(segments)]
    faces.extend((tuple(reversed(range(segments))),tuple(range(segments,2*segments))))
    return finish(builder.add_mesh(name,verts,faces,material))


def lathe(builder,name,profile,material,segments=48):
    # Revolve x/r cross section around X: wheel and pulley geometry.
    verts=[(x,r*math.cos(2*math.pi*i/segments),r*math.sin(2*math.pi*i/segments))
           for x,r in profile for i in range(segments)]
    faces=[]
    for j in range(len(profile)-1):
        for i in range(segments):
            k=(i+1)%segments
            faces.append((j*segments+i,j*segments+k,(j+1)*segments+k,(j+1)*segments+i))
    return finish(builder.add_mesh(name,verts,faces,material))
