#!/usr/bin/env python3
"""Structural and actual-renderer gates for every selectable vehicle.

Original authoring cooks remain intact. The installed surface bake must match
their hashes, retain every receiver triangle, and represent every removed skin.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from bake_vehicle_surfaces import ROOT, BAKED_MODELS, read_mesh, components

ALL_MODELS=('alder_wayfarer',*BAKED_MODELS,'car5','car8','ambulance')
OUT=ROOT/'build/vehicle-surface-qa'


def structural():
    reports=[]
    for name in ALL_MODELS:
        model=ROOT/f'assets/models/vehicles/{name}'
        texture=ROOT/f'assets/textures/vehicles/{name}'
        stem='body_surface' if name in BAKED_MODELS else 'body'
        v,ids=read_mesh(model/(stem+'.emesh'))
        assert np.isfinite(v).all()
        assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all()
        if name in BAKED_MODELS:
            report=json.loads((texture/'body_surface.json').read_text())
            assert report['source_mesh_sha256']==hashlib.sha256((model/'body.emesh').read_bytes()).hexdigest(),name+' stale mesh bake'
            assert report['source_texture_sha256']==hashlib.sha256((texture/'body.png').read_bytes()).hexdigest(),name+' stale paint bake'
            assert not report['unpainted_components'],name+' lost a detail'
            original,oi=read_mesh(model/'body.emesh')
            cc=components(original,oi)
            removed={f for c in report['removed_components'] for f in cc[c]}
            assert len(ids)==len(oi)-len(removed)
            assert len(ids)==report['triangles']
            # Every retained source triangle survives with identical vertices;
            # only the two reviewed GLM front stations may gain lamp backing.
            expected=[]
            for i,t in enumerate(original[oi,:3]):
                if i in removed:continue
                if name in ('glm_zip','glm_lunge') and (np.abs(t[:,2]-(2.5 if name=='glm_zip' else 2.75))<1e-5).any():continue
                expected.append(tuple(sorted(tuple(np.round(p,5)) for p in t)))
            actual={tuple(sorted(tuple(np.round(p,5)) for p in t)) for t in v[ids,:3]}
            assert all(t in actual for t in expected),name+' changed a receiver/arch/hood'
            with Image.open(texture/(stem+'.png')) as atlas:
                assert atlas.size==(256,256) and atlas.mode=='RGBA'
                assert atlas.getchannel('A').getextrema()==(255,255)
            reports.append(report)
        else:
            reports.append(dict(model=name,status='existing body texture retained; shared glow fix applied'))
    return reports


def render(name,label,yaw=190,zone=None,contact=None,strength=.85,lamp=None,frames=30):
    stem='body_surface' if name in BAKED_MODELS else 'body'
    path=OUT/(name+'-'+label+'.png')
    cmd=[str(ROOT/'build/bin/apricot_asset_lab'),'--model',f'models/vehicles/{name}/{stem}.emesh',
         '--texture',f'textures/vehicles/{name}/{stem}.png','--yaw',str(yaw),
         '--frames',str(frames),'--screenshot',str(path)]
    if zone is not None:cmd+=['--damage-zone',str(zone),'--damage-strength',str(strength)]
    if contact is not None:cmd+=['--damage-contact',*[str(x) for x in contact]]
    if lamp is not None:cmd+=['--lamp-preview',str(lamp)]
    result=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
    (OUT/(name+'-'+label+'.log')).write_text(result.stdout+result.stderr)
    result.check_returncode()
    assert '0 GL errors' in result.stdout
    return np.asarray(Image.open(path).convert('RGB'))


def renderer_checks():
    results=[]
    for name in ALL_MODELS:
        clean=render(name,'clean')
        front=render(name,'front-hit',zone=0,contact=(-.7,.45,-.8))
        side=render(name,'side-hit',yaw=212,zone=7,contact=(-1,.7,0))
        render(name,'rear-hit',yaw=32,zone=3,contact=(-.7,.45,.8))
        # Lamp 1 is the near-side lamp at this yaw. A severe front dent can
        # legitimately hide the far-side lamp behind the grille (Halcyon).
        glow=render(name,'front-glow',zone=0,contact=(-.7,.45,-.8),lamp=1)
        changed=np.any(front!=glow,axis=-1)
        assert changed.sum()>8,name+' glow invisible'
        background=front[0,0]
        assert not (changed&np.all(front==background,axis=-1)).any(),name+' glow left the body silhouette'
        repaired=render(name,'repaired',zone=0,contact=(-.7,.45,-.8),strength=0)
        assert np.array_equal(clean,repaired),name+' repair changed paint'
        stable=render(name,'stable-glow',zone=0,contact=(-.7,.45,-.8),lamp=1,frames=90)
        assert np.array_equal(glow,stable),name+' glow flickered'
        assert np.count_nonzero(np.any(clean!=front,axis=-1))>20,name+' damage test did not deform body'
        results.append(dict(model=name,glow_pixels=int(changed.sum()),repair='identical',stability='identical',gl_errors=0))
        print(name,'render checks passed',flush=True)
    # Real-renderer contact sheet, not a replacement rasterizer.
    sheet=Image.new('RGB',(960,240*len(ALL_MODELS)),(15,17,20))
    draw=ImageDraw.Draw(sheet)
    for row,name in enumerate(ALL_MODELS):
        for col,label in enumerate(('front-hit','side-hit','rear-hit')):
            with Image.open(OUT/(name+'-'+label+'.png')) as img:
                # Engine captures include the full inspection viewport. This
                # presentation crop does not participate in numerical checks.
                img=img.crop((int(img.width*.30),int(img.height*.40),int(img.width*.72),int(img.height*.64)))
                img.thumbnail((320,210))
                sheet.paste(img,(col*320,row*240))
            draw.text((col*320+5,row*240+214),name+' '+label,fill=(235,225,210))
    sheet.save(OUT/'damaged-fleet.png')
    return results


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--render',action='store_true')
    args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    report={'structural':structural()}
    if args.render:report['renderer']=renderer_checks()
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Fleet surface checks passed:',len(ALL_MODELS),'models')
