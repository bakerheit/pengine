#!/usr/bin/env python3
"""Structural gates for the Rearloader; these do not assign a quality class."""
import json
import numpy as np
from PIL import Image
from harrow_rearloader_spec import ROOT,SLUG,SHAPE as S,BODY_TRIANGLE_LIMIT
from make_vesper_vx91_assets import read_emesh

def validate():
    folder=ROOT/'assets/models/vehicles'/SLUG;metrics={}
    names=('body','body_open','driver_door','windshield','rear_glass','driver_glass','passenger_glass','front_wheel','rear_wheel')
    for name in names:
        vv,ii=read_emesh(folder/(name+'.emesh'));v=np.asarray(vv);ids=np.asarray(ii).reshape(-1,3)
        assert len(v)>0 and len(ids)>0 and ids.min()>=0 and ids.max()<len(v),(name,'indices')
        assert np.isfinite(v).all() and ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all(),(name,'finite UVs')
        ns=np.linalg.norm(v[:,3:6],axis=1);assert ((ns>.99)&(ns<1.01)).all(),(name,'normals')
        tri=v[ids,:3];area=np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)
        assert (area>1e-11).all(),(name,'degenerate',int((area<=1e-11).sum()))
        lo=v[:,:3].min(0);hi=v[:,:3].max(0)
        metrics[name]={'vertices':len(v),'triangles':len(ids),'min':lo.tolist(),'max':hi.tolist()}
        if name in ('front_wheel','rear_wheel'):
            assert max(abs(lo[1]+.60),abs(hi[1]-.60),abs(lo[2]+.60),abs(hi[2]-.60))<.015,(name,'radius')
    # Each outer side opening must be empty at its wheel center. A center
    # chassis/axle hit is allowed; a solid near-side panel is not.
    vv,ii=read_emesh(folder/'body.emesh');bv=np.asarray(vv);bt=bv[np.asarray(ii).reshape(-1,3),:3]
    def inside(p,t):
        a,b,c=t;cross=lambda q,r:q[0]*r[1]-q[1]*r[0]
        values=[cross(b-a,p-a),cross(c-b,p-b),cross(a-c,p-c)]
        return min(values)>1e-7 or max(values)<-1e-7
    for axle in (S['front_axle'],S['rear_axle']):
        for side in (-1,1):
            outer=bt[(side*bt[:,:,0]>1.10).all(1)]
            assert not any(inside(np.array((axle,.60)),t[:,[2,1]]) for t in outer),('blocked wheel opening',side,axle)
    assert metrics['body']['triangles']<BODY_TRIANGLE_LIMIT,'body triangle budget'
    assert 7.50<metrics['body']['max'][2]-metrics['body']['min'][2]<7.90,'length'
    assert 3.50<metrics['body']['max'][1]<3.70,'roof'
    assert metrics['rear_wheel']['max'][0]-metrics['rear_wheel']['min'][0]>.56,'dual tires missing'
    assert metrics['front_wheel']['max'][0]-metrics['front_wheel']['min'][0]<.44,'front single tire'
    with Image.open(ROOT/'assets/textures/vehicles'/SLUG/'body.png') as im:assert im.size==(256,256) and im.mode=='RGBA'
    assert (folder/'source.blend').is_file(),'editable source missing'
    # Require all references to have existed before this asset cook.
    ref=ROOT/'docs/design/references'/SLUG
    for n in ('concept-working','front','rear','left','right','top','front-three-quarter','rear-three-quarter'):
        assert (ref/(n+'.png')).is_file(),('missing reference',n)
    out={'status':'structural pass; visual/runtime acceptance separate','parts':metrics,'checks':['finite nondegenerate triangles','unit normals','atlas UV bounds','256x256 RGBA atlas','two-axle single-front/dual-rear wheel parts','four clear outer wheel openings','required panes and solid door exports','editable Blender source','full directional references']}
    path=ROOT/'build/harrow_rearloader-validation.json';path.write_text(json.dumps(out,indent=2)+'\n');return out
if __name__=='__main__':print(json.dumps(validate(),indent=2))
