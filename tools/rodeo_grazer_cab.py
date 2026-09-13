"""Continuous stamped cab and door surfaces; construction axes X/Y/Z.

Shared vertices close the fixed seams before thickness and edge rounding.
Door apertures retain 4-6 mm shut lines and separate articulated glass.
"""
import bpy
import bmesh
from mathutils import Vector


def side_x(h):
    return .90-max(0,h-1.08)*.17


class Sheet:
    def __init__(self):
        self.vertices=[]
        self.faces=[]
        self.keys=[]
        self.index={}

    def face(self,points,key='PAINT'):
        indices=[]
        for point in points:
            point=tuple(round(float(v),7) for v in point)
            if point not in self.index:
                self.index[point]=len(self.vertices)
                self.vertices.append(point)
            indices.append(self.index[point])
        self.faces.append(indices)
        self.keys.append(key)

    def ring(self,outer,inner,key='PAINT'):
        for i in range(len(outer)):
            j=(i+1)%len(outer)
            self.face([outer[i],outer[j],inner[j],inner[i]],key)

    def finish(self,name,material,thickness=.03,edge_radius=.008,edge_segments=2):
        # Split coplanar joins at every shared station before welding. This
        # prevents T-junction cracks when a roof edge meets several side panels.
        points=[Vector(v) for v in self.vertices]
        faces=[]
        for face in self.faces:
            split=[]
            for i,a in enumerate(face):
                b=face[(i+1)%len(face)];delta=points[b]-points[a]
                split.append(a)
                mids=[]
                for k,p in enumerate(points):
                    if k in (a,b):continue
                    t=(p-points[a]).dot(delta)/delta.length_squared
                    if 1e-6<t<1-1e-6 and (p-points[a]-t*delta).length<1e-6:
                        mids.append((t,k))
                split.extend(k for _,k in sorted(mids))
            faces.append(split)
        data=bpy.data.meshes.new(name)
        data.from_pydata(self.vertices,[],faces);data.update()
        obj=bpy.data.objects.new(name,data);bpy.context.scene.collection.objects.link(obj)
        keys=list(dict.fromkeys(self.keys))
        for key in keys:data.materials.append(material(key))
        for f,key in zip(data.polygons,self.keys):f.material_index=keys.index(key)
        bm=bmesh.new();bm.from_mesh(data)
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        # Roof or outside door flank determines the open sheet's orientation.
        anchor=max(bm.faces,key=lambda f:f.calc_center_median().z) if 'Cab' in name else max(bm.faces,key=lambda f:abs(f.calc_center_median().x))
        direction=Vector((0,0,1)) if 'Cab' in name else Vector((1 if anchor.calc_center_median().x>0 else -1,0,0))
        if anchor.normal.dot(direction)<0:
            bmesh.ops.reverse_faces(bm,faces=list(bm.faces))
        bm.to_mesh(data);bm.free()
        bpy.context.view_layer.objects.active=obj
        mod=obj.modifiers.new('Continuous inner sheet and aperture returns','SOLIDIFY')
        mod.thickness=thickness;mod.offset=-1;mod.use_even_offset=True
        bpy.ops.object.modifier_apply(modifier=mod.name)
        mod=obj.modifiers.new('Round connected stamping','BEVEL')
        mod.width=edge_radius;mod.segments=edge_segments;mod.limit_method='ANGLE';mod.angle_limit=.7
        bpy.ops.object.modifier_apply(modifier=mod.name)
        bm=bmesh.new();bm.from_mesh(data)
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
        assert all(e.is_manifold for e in bm.edges),(name,'open structural edge')
        remaining=set(bm.verts);stack=[next(iter(remaining))]
        while stack:
            v=stack.pop()
            if v not in remaining:continue
            remaining.remove(v)
            stack.extend(e.other_vert(v) for e in v.link_edges)
        assert not remaining,(name,'disconnected structural pieces')
        bm.to_mesh(data);bm.free()
        obj['continuous_shell']=True
        return obj


def inset(points,amount=.004):
    center=sum((Vector(p) for p in points),Vector())/len(points)
    return [Vector(p)+(center-Vector(p)).normalized()*amount for p in points]


def cab(material):
    sheet=Sheet();edge_h=1.752;edge_x=side_x(edge_h)
    front=.407;rear=-.66
    glass={}
    for s in (-1,1):
        def side(poly):return [(s*side_x(h),y,h) for y,h in poly]
        # Quarter panel, window surround and upper door rail share their edges.
        sheet.face(side([(-.70,.43),(-.284,.43),(-.284,1.08),(-.70,1.08)]),'SIDE')
        outer=side([(-.70,1.08),(-.284,1.08),(-.284,edge_h),(rear,edge_h)])
        inner=side([(-.625,1.15),(-.35,1.15),(-.35,1.67),(-.57,1.67)])
        sheet.ring(outer,inset(inner))
        name='driver' if s>0 else 'passenger'
        glass[name+'_rear_glass']=[(x-s*.014,y,h) for x,y,h in inner]
        sheet.face(side([(-.284,1.720),(.415,1.720),(front,edge_h),(-.284,edge_h)]))
        sheet.face(side([(.925,.43),(.940,.43),(.940,1.065),(.960,1.08),(front,edge_h),(.415,1.720),(.925,1.08)]))
        # Door sill connects the front and rear jamb without filling the opening.
        sheet.face(side([(-.70,.40),(.940,.40),(.940,.43),(-.70,.43)]),'SIDE')

    rows=[(-.66,1.752),(-.62,1.770),(-.50,1.78),(.25,1.78),(.365,1.770),(.407,1.752)]
    roof=[]
    for y,h in rows:
        roof.append([(edge_x*q,y,edge_h+(h-edge_h)*weight) for q,weight in
                     [(-1,0),(-.94,.45),(-.72,.90),(0,1),(.72,.90),(.94,.45),(1,0)]])
    for a,b in zip(roof,roof[1:]):
        for i in range(6):sheet.face([a[i],a[i+1],b[i+1],b[i]])
    windshield=[(-.858,.927,1.10),(.858,.927,1.10),(.75,.431,1.70),(-.75,.431,1.70)]
    sheet.ring([(-.90,.960,1.08),(.90,.960,1.08),(edge_x,front,edge_h),(-edge_x,front,edge_h)],inset(windshield))
    # Windshield cowl has a shared return to the hood-height edge.
    sheet.face([(-.90,.940,1.065),(.90,.940,1.065),(.90,.960,1.08),(-.90,.960,1.08)])
    glass['windshield']=windshield
    back=[(-.815,-.685,1.16),(.815,-.685,1.16),(.727,-.651,1.64),(-.727,-.651,1.64)]
    sheet.ring([(-.90,-.70,1.08),(.90,-.70,1.08),(edge_x,rear,edge_h),(-edge_x,rear,edge_h)],inset(back))
    sheet.face([(-.90,-.70,.40),(.90,-.70,.40),(.90,-.70,1.08),(-.90,-.70,1.08)],'SIDE')
    glass['rear_glass']=back
    return sheet.finish('Cab continuous shell',material),glass


def door(s,material):
    sheet=Sheet();lo=-.28;hi=.92
    levels=[(.43,.867),(.49,.889),(.55,.901),(.69,.914),(.81,.922),
            (.91,.918),(.975,.914),(1.035,.908),(1.06,.903),(1.08,.90)]
    rows=[]
    for h,x in levels:
        rows.append([(s*(x-(.008 if i in (0,3) and h<1.08 else 0)),y,h)
                     for i,y in enumerate([lo,lo+.045,hi-.045,hi])])
    for a,b in zip(rows,rows[1:]):
        for i in range(3):sheet.face([a[i],a[i+1],b[i+1],b[i]],'SIDE')
    outer=[(s*side_x(h),y,h) for y,h in [(lo,1.08),(hi,1.08),(.410,1.716),(lo,1.716)]]
    inner=[(s*side_x(h),y,h) for y,h in [(-.22,1.15),(.805,1.15),(.348,1.67),(-.22,1.67)]]
    sheet.ring(outer,inset(inner))
    name='driver' if s>0 else 'passenger'
    return sheet.finish(name+' continuous door',material,.035),[(x-s*.014,y,h) for x,y,h in inner]
