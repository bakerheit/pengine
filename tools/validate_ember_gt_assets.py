#!/usr/bin/env python3
"""Check the cooked car, continuous atlas, hood and actual turned tire volumes."""
import json,math
import numpy as np
from PIL import Image
from ember_gt_spec import ROOT,MODEL,TEXTURE,WHEELS,DOOR,DRIVER,cell
from make_vesper_vx91_assets import read_emesh

def validate():
    report={}
    for path in sorted(MODEL.glob('*.emesh')):
        verts,indices=read_emesh(path);v=np.asarray(verts);ids=np.asarray(indices).reshape(-1,3)
        assert len(ids)>0 and np.isfinite(v).all(),path
        assert ids.min()>=0 and ids.max()<len(v),path
        assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all(),path
        t=v[ids,:3]
        areas=np.linalg.norm(np.cross(t[:,1]-t[:,0],t[:,2]-t[:,0]),axis=1)
        assert (areas>1e-10).all(),(path,'degenerate triangles')
        report[path.stem]=dict(triangles=len(ids),minimum=v[:,:3].min(0).tolist(),maximum=v[:,:3].max(0).tolist())
    with Image.open(TEXTURE) as atlas:assert atlas.size==(256,256) and atlas.mode=='RGBA'
    # Inspect the actual cooked control surfaces, not just the declared anchors.
    # Interior alloy is shared with door releases, so restrict to the footwell
    # and steering area, clear of both door cards and the center console.
    verts,indices=read_emesh(MODEL/'body_open.emesh');control_v=np.asarray(verts)
    cx,cy,cw,ch=cell('Interior alloy')
    alloy=(control_v[:,6]>cx/256)&(control_v[:,6]<(cx+cw)/256)&(control_v[:,7]>1-(cy+ch)/256)&(control_v[:,7]<1-cy/256)
    controls=control_v[alloy,:3]
    cabin=(abs(controls[:,0])>.14)&(abs(controls[:,0])<.65)&(controls[:,1]>.23)&(controls[:,1]<.91)&(controls[:,2]>.30)&(controls[:,2]<.90)
    assert cabin.any() and (controls[cabin,0]>0).all(),'controls must be on driver left, never passenger right'
    control_ids=np.asarray(indices).reshape(-1,3)
    control_tri=control_v[control_ids[alloy[control_ids[:,0]]],:3]
    samples=np.concatenate([control_tri[:,0]*(1-a-b)+control_tri[:,1]*a+control_tri[:,2]*b
                            for a in np.linspace(0,1,9) for b in np.linspace(0,1-a,9)])
    for name in ['wheel','brake','accelerator']:
        anchor=np.asarray(DRIVER[name]);distance=np.linalg.norm(samples-anchor,axis=1)
        assert distance.min()<.035,(name,'missing cooked control surface')
    verts,indices=read_emesh(MODEL/'body.emesh');v=np.asarray(verts);tri=v[np.asarray(indices).reshape(-1,3),:3]
    cloud=[]
    for t in tri:
        if t[:,1].min()>.765 or abs(t[:,2].mean())<.92:continue
        count=max(1,math.ceil(max(np.linalg.norm(t[i]-t[j]) for i,j in [(0,1),(0,2),(1,2)])/.025))
        for i in range(count+1):
            js=np.arange(count+1-i)[:,None]/count
            cloud.append(t[0]+i/count*(t[1]-t[0])+js*(t[2]-t[0]))
    points=np.concatenate(cloud);poses=0
    # The tire is an annulus; brakes and spokes correctly occupy its center.
    for z in [WHEELS['front_z'],WHEELS['rear_z']]:
        for side in [-1,1]:
            rel=points-[side*WHEELS['x'],WHEELS['arch_y'],z]
            for angle in (np.linspace(-.57,.57,33) if z>0 else [0]):
                lateral=rel[:,0]*np.cos(angle)+rel[:,2]*np.sin(angle)
                longitudinal=-rel[:,0]*np.sin(angle)+rel[:,2]*np.cos(angle)
                radius=np.sqrt(longitudinal**2+rel[:,1]**2)
                bad=(abs(lateral)<.132)&(radius<.358)&(radius>.277)
                assert not bad.any(),('tire/body contact',z,side,float(angle),points[bad][:3].tolist())
                poses+=1
    def cover(x,z):
        p=np.array([x,z])
        for t in tri:
            if t[:,1].min()<.68:continue
            q=t[:,[0,2]];cross=lambda a,b:a[0]*b[1]-a[1]*b[0]
            ds=[cross(q[(i+1)%3]-q[i],p-q[i]) for i in range(3)]
            if min(ds)>1e-8 or max(ds)<-1e-8:return True
        return False
    for x in [-.84,-.70,-.55,-.25,.03,.29,.55,.70,.84]:
        for z in [1.23,1.41,-1.20,-1.38]:assert cover(x,z),('hood/deck hole',x,z)
    report.update(wheels=WHEELS,door=DOOR,driver=DRIVER,steering_poses=poses,
        checks=['finite triangles and atlas UVs','four clear tire annuli at full steering lock',
                'continuous hood and rear deck above both axles','256x256 RGBA material atlas',
                'wheel, brake and accelerator geometry only in the left driver footwell'])
    (ROOT/'build/ember-gt-fit-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Ember asset validation passed:',poses,'wheel poses;',len(points),'surface samples')
    return report

if __name__=='__main__':validate()
