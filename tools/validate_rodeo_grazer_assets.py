#!/usr/bin/env python3
"""Validate actual Grazer triangles, open bed, glazing and swept tire fit."""
import json,math
import numpy as np
from PIL import Image
from rodeo_grazer_spec import ROOT,MODEL,TEXTURE,WHEELS,SHAPE,DRIVER,ART_DIRECTION,EMBLEMS,EMBLEM_DEPTH,REGIONS,COLORS
from make_vesper_vx91_assets import read_emesh
report={}
meshes={}
mesh_data={}
for path in MODEL.glob('*.emesh'):
    verts,ids=read_emesh(path);v=np.asarray(verts);ids=np.asarray(ids).reshape(-1,3)
    assert len(ids)>0 and np.isfinite(v).all() and ids.max()<len(v),path
    assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all(),path
    tri=v[ids,:3];area=np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)
    assert (area>1e-10).all(),(path,'degenerate faces')
    meshes[path.stem]=tri
    mesh_data[path.stem]=(v,ids)
    report[path.stem]=dict(triangles=len(ids),minimum=v[:,:3].min(0).tolist(),maximum=v[:,:3].max(0).tolist())
with Image.open(TEXTURE) as im:
    assert im.size==(256,256) and im.mode=='RGBA'
    for key in ('BADGE_DARK','BADGE_RUST','BADGE_METAL'):
        cell=im.crop(REGIONS[key])
        assert cell.getextrema()==tuple((c,c) for c in (*COLORS[key],255)),(key,'enamel swatch changed')
assert 'body' in meshes and 'body_open' in meshes,meshes.keys()
assert len(meshes['body'])<15000,('body triangle budget',len(meshes['body']))

# Inspect the actual cooked faces on both render paths. The old rectangular
# wordmark, a filled-in counter, or a mirrored rear R must fail this check.
for part in ('body','body_open'):
    vertices,indices=mesh_data[part]
    uv=vertices[indices,6:8].mean(axis=1)
    for name,mount in EMBLEMS.items():
        cx,cy,cz=mount['center'];side=mount['normal']
        tri=meshes[part]
        badge=(uv[:,0]>.75)&(uv[:,1]<.125)&(side*(tri[:,:,2].mean(axis=1)-cz)>0)
        faces=tri[badge];face_uv=uv[badge]
        assert 40<len(faces)<300,(part,name,'missing or excessive emblem geometry',len(faces))
        assert abs(faces[:,:,0].min()-(-mount['width']/2))<.004
        assert abs(faces[:,:,0].max()-mount['width']/2)<.004
        def badge_hit(u,v):
            point=np.array([cx+side*(u-.5)*mount['width'],cy+(v-.5)*mount['height']])
            hits=[]
            for t,texture_uv in zip(faces,face_uv):
                q=t[:,:2];matrix=np.column_stack((q[1]-q[0],q[2]-q[0]))
                if abs(np.linalg.det(matrix))<1e-10:continue
                a,b=np.linalg.solve(matrix,point-q[0])
                if a>=-1e-5 and b>=-1e-5 and a+b<=1+1e-5:
                    depth=side*((t[0]+a*(t[1]-t[0])+b*(t[2]-t[0]))[2]-cz)
                    hits.append((depth,texture_uv[0]*256))
            return max(hits,default=None)
        # Face-on locations deliberately describe the visible identity.
        for label,u,v,key in [('left stem',.08,.20,'BADGE_DARK'),
                              ('top bowl',.40,.92,'BADGE_DARK'),
                              ('orange right leg',.80,.12,'BADGE_RUST')]:
            hit=badge_hit(u,v);x0,_,x1,_=REGIONS[key]
            assert hit is not None and x0<hit[1]<x1,(part,name,label,hit)
            assert EMBLEM_DEPTH<=hit[0]<EMBLEM_DEPTH+.004,(part,name,'badge face depth',hit)
        assert badge_hit(.39,.70) is None,(part,name,'R counter filled in')
        assert badge_hit(.37,.15) is None,(part,name,'space between legs filled in')

def side_ray_covers(tri,side,z,y):
    """Shoot from outside along X; reject hits which only reach the far side."""
    origin=np.array([side*1.15,y,z]);direction=np.array([-side,0.,0.])
    edge_a=tri[:,1]-tri[:,0];edge_b=tri[:,2]-tri[:,0]
    perpendicular=np.cross(np.broadcast_to(direction,edge_b.shape),edge_b)
    determinant=np.einsum('ij,ij->i',edge_a,perpendicular)
    valid=np.abs(determinant)>1e-9
    inverse=np.zeros_like(determinant);np.divide(1,determinant,out=inverse,where=valid)
    offset=origin-tri[:,0]
    first=inverse*np.einsum('ij,ij->i',offset,perpendicular)
    cross=np.cross(offset,edge_a)
    second=inverse*np.einsum('j,ij->i',direction,cross)
    distance=inverse*np.einsum('ij,ij->i',edge_b,cross)
    hit=valid&(first>=-1e-6)&(second>=-1e-6)&(first+second<=1+1e-6)&(distance>=-1e-6)
    # A hit past the body centerline cannot close this side's seam.
    exterior_x=side*(origin[0]+distance*direction[0])
    return bool(np.any(hit&(exterior_x>=.70)))

def expect_side_rays(mesh,samples,covered):
    for label,(z,y) in samples.items():
        for side in (-1,1):
            actual=side_ray_covers(mesh,side,z,y)
            assert actual==covered,(label,'driver' if side>0 else 'passenger',z,y,actual)

# These are intentional structural seams of the fixed open-door shell. The A-pillar
# and quarter-lower rays fall through the pre-continuity cooked model, while remaining
# away from the intended door and glass apertures.
expect_side_rays(meshes['body_open'],{
    'roof shoulder':(-.1780,1.7604),
    'fixed A-pillar return':(.5000,1.5700),
    'quarter lower jamb':(-.3000,1.0650),
},True)
# The closed body must fill the door belt, but the open vehicle must retain its
# door and window apertures for articulated doors and separate glazing.
expect_side_rays(meshes['body'],{'closed door beltline':(.1000,1.0350)},True)
expect_side_rays(meshes['body_open'],{
    'open door aperture':(.1000,1.0350),
    'front side window aperture':(.1000,1.4000),
    'quarter window aperture':(-.4800,1.4000),
},False)

tri=meshes['body']
cloud=[]
for t in tri:
    if t[:,1].min()>.85 or abs(t[:,2].mean())<.9:continue
    n=max(1,math.ceil(max(np.linalg.norm(t[i]-t[j]) for i,j in [(0,1),(0,2),(1,2)])/.025))
    for i in range(n+1):
        js=np.arange(n+1-i)[:,None]/n;cloud.append(t[0]+i/n*(t[1]-t[0])+js*(t[2]-t[0]))
cloud=np.concatenate(cloud);poses=0
for axle in [1.43,-1.50]:
    for side in [-1,1]:
        p=cloud-[side*.82,.405,axle]
        for angle in (np.linspace(-.52,.52,33) if axle>0 else [0]):
            x=p[:,0]*math.cos(angle)+p[:,2]*math.sin(angle)
            z=-p[:,0]*math.sin(angle)+p[:,2]*math.cos(angle)
            r=np.hypot(p[:,1],z)
            bad=(abs(x)<.127)&(r<.393)&(r>.292)
            assert not bad.any(),('tire intersection',axle,side,float(angle),cloud[bad][:3].tolist())
            poses+=1
# The center of the load bed must have a floor but no cover above it.
def over(x,z):
    hits=[]
    for t in tri:
        q=t[:,[0,2]];m=np.column_stack((q[1]-q[0],q[2]-q[0]))
        if abs(np.linalg.det(m))<1e-8:continue
        a,b=np.linalg.solve(m,np.array([x,z])-q[0])
        if a>=0 and b>=0 and a+b<=1:hits.append(t[0,1]+a*(t[1,1]-t[0,1])+b*(t[2,1]-t[0,1]))
    return hits
for z in [-2.25,-1.50,-.95]:
    hits=over(.09,z);assert hits and .64<max(hits)<.68,('covered/missing bed',z,hits)
report.update(art_direction=ART_DIRECTION,shape=SHAPE,wheels=WHEELS,driver=DRIVER,steering_poses=poses,
              emblems=EMBLEMS,
              checks=['finite nondegenerate mesh and atlas','body below 15000 triangles',
                      'front and rear Rodeo R, open counters, correct handedness and enamel colors on both body paths',
                      'outboard cab seams closed with door and window apertures preserved',
                      '68 swept wheel poses','open load bed with floor'])
(ROOT/'build/grazer-fit-report.json').write_text(json.dumps(report,indent=2)+'\n')
print('Grazer assets passed:',poses,'wheel poses;',len(cloud),'surface samples')
