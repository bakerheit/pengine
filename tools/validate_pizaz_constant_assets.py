#!/usr/bin/env python3
"""Focused contracts for PIZAZ's cooked hierarchy, atlas, fit and apertures."""
import json,math
import numpy as np
from PIL import Image
from pizaz_constant_spec import ROOT,MODEL,TEXTURE,WHEELS
from make_vesper_vx91_assets import read_emesh
names=['body','body_open','body_drive','driver_door','passenger_door','windshield','rear_glass','driver_glass','passenger_glass','driver_rear_glass','passenger_rear_glass','front_wheel','rear_wheel']
parts={};report={}
for name in names:
    vertices,indices=read_emesh(MODEL/(name+'.emesh'));v=np.asarray(vertices);ix=np.asarray(indices)
    assert len(v)>0 and len(ix)%3==0 and ix.max()<len(v),name
    assert np.isfinite(v).all() and ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all(),name
    tri=v[ix.reshape(-1,3),:3]
    assert (np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)>1e-10).all(),name
    parts[name]=(v,tri);report[name]={'triangles':len(ix)//3,'min':v[:,:3].min(axis=0).tolist(),'max':v[:,:3].max(axis=0).tolist()}
body=parts['body'][0];lo=body[:,:3].min(axis=0);hi=body[:,:3].max(axis=0)
assert 4.6<hi[2]-lo[2]<4.75 and 1.98<hi[0]-lo[0]<2.06 and 1.30<hi[1]<1.34
assert abs(lo[0]+hi[0])<.003 and len(parts['body'][1])<40000
for name in ['front_wheel','rear_wheel']:
    v=parts[name][0];r=np.hypot(v[:,1],v[:,2]).max();assert abs(r-.32)<.003 and abs(v[:,0].min()+v[:,0].max())<.04
# Side rays through the inner wheel opening must miss the outer body.
tri=parts['body'][1]
for side in [-1,1]:
    outer=tri[(tri[:,:,0]*side>.72).all(axis=1)]
    for axle in [-1.38,1.38]:
        for dz,dy in [(0,0),(-.18,0),(.18,0),(0,.20),(-.12,.14),(.12,.14)]:
            p=np.array([.32+dy,axle+dz]);a=outer[:,0,1:3];b=outer[:,1,1:3];c=outer[:,2,1:3]
            u=b-a;v=c-a;d=p-a;det=u[:,0]*v[:,1]-u[:,1]*v[:,0];valid=np.abs(det)>1e-9
            q=np.zeros(len(det));r=q.copy();q[valid]=(d[valid,0]*v[valid,1]-d[valid,1]*v[valid,0])/det[valid];r[valid]=(u[valid,0]*d[valid,1]-u[valid,1]*d[valid,0])/det[valid]
            assert not (valid&(q>1e-5)&(r>1e-5)&(q+r<1-1e-5)).any(),(side,axle,dz,dy)
with Image.open(TEXTURE) as im:assert im.size==(256,256) and im.mode=='RGBA'
report['checks']=['finite triangulated meshes','UV range','body fit and symmetry','centered custom wheels','24 outer wheel-opening rays','256x256 RGBA atlas','body budget below 40000 triangles']
(ROOT/'build/pizaz-constant-fit-report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PIZAZ fit checks passed; body triangles:',report['body']['triangles'])
