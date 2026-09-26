#!/usr/bin/env python3
"""Cook and check the three selected 1991 vehicle concepts."""
import argparse
import json
import math
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from candidate_1991_spec import ATLAS, BODY_TRIANGLE_LIMIT, REGIONS, ROOT, SHAPES
from make_vesper_vx91_assets import read_emesh
from render_firetruck_preview import raster_view, read_part, transformed

BLENDER=Path('/Applications/Blender.app/Contents/MacOS/Blender')


def texture(slug):
    s=SHAPES[slug]
    colors={'PAINT':s['paint'],'TOP':s['top'],'CREAM':s['cream'],
            'CLAD':s['clad'],'DARK':(24,27,30),'CABIN':(62,66,67),
            'SEAT':s['seat'],'HEADLINER':(153,151,137),
            'METAL':(158,166,164),'LAMP':(222,224,208),
            'AMBER':(218,139,42),'RED':(160,37,34),
            'WINDOW':(74,98,108),'TYRE':(27,28,29),
            'STEEL':(53,59,61) if s['kind']=='wrecker' else (177,181,177),
            'STRIPE':(22,24,25)}
    im=Image.new('RGBA',(ATLAS,ATLAS))
    p=im.load()
    for name,(x0,y0,x1,y1) in REGIONS.items():
        r,g,b=colors[name]
        for y in range(y0,y1):
            shade=.97+.08*(y-y0)/(y1-y0) if name in ('PAINT','TOP','CREAM') else 1
            for x in range(x0,x1):
                grain=(x*7+y*3)%5-2 if name in ('CABIN','SEAT','HEADLINER') else 0
                p[x,y]=(min(255,round(r*shade)+grain),
                        min(255,round(g*shade)+grain),
                        min(255,round(b*shade)+grain),255)
    d=ImageDraw.Draw(im)
    for name in ('LAMP','AMBER','RED'):
        x0,y0,x1,y1=REGIONS[name]
        for py in range(y0,y1):
            for px in range(x0,x1):
                u=(px-x0+.5)/64;v=(py-y0+.5)/64
                edge=min(u,1-u,v,1-v)
                if name=='LAMP':
                    centres=(.26,.74) if s['kind']=='van' else (.5,)
                    rr=min(math.hypot((u-c)/(.27 if len(centres)>1 else .54),(v-.49)/.51) for c in centres)
                    value=88+112*max(0,1-rr*.52)+24*(1-v)
                    value-=28*math.exp(-((v-.66)/.16)**2)*max(0,1-rr)
                    value+=9 if (px-x0)%4==0 else -3
                    value-=6 if (py-y0)%8==0 else 0
                    if edge<.055:value=70+edge*1100
                    value=max(45,min(231,int(value)))
                    p[px,py]=(value,min(255,value+3),min(255,value+4),255)
                else:
                    base=(192,104,17) if name=='AMBER' else (125,18,14)
                    fac=.80+.30*(1-v)+(.16 if ((px-x0)//3+(py-y0)//4)%2 else 0)
                    if edge<.055:fac*=.58
                    p[px,py]=tuple(min(255,round(c*fac)) for c in base)+(255,)
    # Restrained fabric seams; no painted cartoon highlights.
    x0,y0,x1,y1=REGIONS['SEAT']
    seam=tuple(max(0,c-13) for c in s['seat'])+(255,)
    for x in (x0+13,x1-14):d.line((x,y0+8,x,y1-9),fill=seam,width=1)
    for name in ('STEEL','METAL'):
        x0,y0,x1,y1=REGIONS[name]
        for y in range(y0,y1):
            shade=1+.055*math.sin((y-y0)*.19)
            for x in range(x0,x1):
                p[x,y]=tuple(min(255,round(c*shade)) for c in colors[name])+(255,)
    path=ROOT/'assets/textures/vehicles'/slug/'body.png'
    path.parent.mkdir(parents=True,exist_ok=True)
    im.save(path)
    return path


def validate(slug):
    s=SHAPES[slug]
    folder=ROOT/'assets/models/vehicles'/slug
    required=['body','body_open','driver_door','windshield','rear_glass',
              'passenger_glass','driver_glass','front_wheel','rear_wheel']
    if s['kind']!='wrecker':required+=['driver_rear_glass','passenger_rear_glass']
    metrics={}
    for name in required:
        vertices,indices=read_emesh(folder/(name+'.emesh'))
        v=np.asarray(vertices,dtype=float)
        ids=np.asarray(indices,dtype=np.int64)
        assert len(v)>0 and len(ids)>0 and len(ids)%3==0,(slug,name,'empty')
        assert ids.min()>=0 and ids.max()<len(v),(slug,name,'indices')
        assert np.isfinite(v).all() and np.all((v[:,6:8]>=0)&(v[:,6:8]<=1)),(slug,name,'UVs')
        normal_lengths=np.linalg.norm(v[:,3:6],axis=1)
        assert np.all((normal_lengths>.99)&(normal_lengths<1.01)),(slug,name,'non-unit normals')
        tri=v[ids.reshape(-1,3),:3]
        area=np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)
        assert (area>1e-10).all(),(slug,name,'degenerate',int((area<=1e-10).sum()))
        if name in ('front_wheel','rear_wheel'):
            # Detail radii must never overwrite the shared running-wheel size.
            measured=np.max(np.abs(v[:,1:3]),axis=0)
            assert np.all(np.abs(measured-s['wheel_radius'])<.012), (slug,name,'wheel radius',measured)
        metrics[name]={'vertices':len(v),'triangles':len(tri)}
    body_v,body_i=read_emesh(folder/'body.emesh')
    body=np.asarray(body_v,dtype=float)
    lo,hi=body[:,:3].min(0),body[:,:3].max(0)
    assert abs((hi[0]+lo[0])*.5)<.08,(slug,'not centered',lo,hi)
    assert 2*s['half_length']-.15 < hi[2]-lo[2] < 2*s['half_length']+.6,(slug,'length',lo,hi)
    assert hi[1]>s['roof']-.05,(slug,'missing roof',hi)
    assert len(body_i)//3<BODY_TRIANGLE_LIMIT,(slug,'budget',len(body_i)//3)
    # The outer side skin must leave the four wheel centers open.
    triangles=body[np.asarray(body_i).reshape(-1,3),:3]
    def point_in_triangle(p,t):
        a,b,c=t
        cross=lambda u,v:u[0]*v[1]-u[1]*v[0]
        signs=[cross(b-a,p-a),cross(c-b,p-b),cross(a-c,p-c)]
        return min(signs)>1e-7 or max(signs)<-1e-7
    for axle in (s['front_axle'],s['rear_axle']):
        for sign in (-1,1):
            outer=triangles[(sign*triangles[:,:,0]>s['half_width']-.035).all(1)]
            assert not any(point_in_triangle(np.array((axle,s['wheel_radius'])),t[:,[2,1]])
                           for t in outer),(slug,'closed wheel arch',axle,sign)
    with Image.open(ROOT/'assets/textures/vehicles'/slug/'body.png') as im:
        assert im.mode=='RGBA' and im.size==(256,256)
    assert (folder/'source.blend').is_file()
    return {'bounds':{'min':lo.tolist(),'max':hi.tolist()},'parts':metrics,
            'checks':['finite triangles and UVs','four outer wheel openings',
                      'transparent pane exports','interior and source scene']}


def preview(slug):
    s=SHAPES[slug]
    folder=ROOT/'assets/models/vehicles'/slug
    atlas=ROOT/'assets/textures/vehicles'/slug/'body.png'
    parts=[read_part(folder/'body.emesh',atlas)]
    for side in (-1,1):
        for axle,name in ((s['front_axle'],'front_wheel'),(s['rear_axle'],'rear_wheel')):
            wheel=read_part(folder/(name+'.emesh'),atlas)
            parts.append(transformed(wheel,1,(side*s['wheel_x'],s['wheel_radius'],axle)))
    out=Image.new('RGB',(1350,450),(23,25,28))
    d=ImageDraw.Draw(out)
    for i,(label,yaw,pitch) in enumerate((('FRONT 3/4',-32,14),('SIDE',-90,4),('REAR 3/4',-148,14))):
        out.paste(raster_view(parts,yaw,pitch,450,415),(450*i,0))
        d.text((450*i+12,426),label,fill=(225,226,221))
    path=ROOT/'build'/f'{slug}-preview.png'
    path.parent.mkdir(exist_ok=True)
    out.save(path)
    return path


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('slugs',nargs='*')
    args=parser.parse_args()
    for slug in args.slugs:
        if slug not in SHAPES:parser.error(f'unknown candidate: {slug}')
    for slug in args.slugs or SHAPES:
        texture(slug)
        subprocess.run([str(BLENDER),'-b','--python-exit-code','1','--python',
                        str(ROOT/'tools/candidate_1991_blender.py'),'--',slug],
                       cwd=ROOT,check=True)
        report=validate(slug)
        image=preview(slug)
        path=ROOT/'build'/f'{slug}-fit-report.json'
        previous=json.loads(path.read_text())
        previous['validation']=report
        previous['preview']=str(image)
        path.write_text(json.dumps(previous,indent=2)+'\n')
        print(slug,'valid',report['parts']['body']['triangles'],'triangles',image)


if __name__=='__main__':main()
