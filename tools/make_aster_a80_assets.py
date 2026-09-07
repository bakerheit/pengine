#!/usr/bin/env python3
"""Cook the original Aster airliner, authored pixel atlas, UV guide and checks."""
import argparse
import json
import math
import subprocess
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from aster_a80_spec import SHAPE, REGIONS, GEAR, ENTRY, PILOT
from bake_vehicle_surfaces import read_mesh

ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/aster_a80'
TEXTURE=ROOT/'assets/textures/vehicles/aster_a80/body.png'

def texture(blockout=False):
    im=Image.new('RGBA',(256,256),(32,40,43,255));d=ImageDraw.Draw(im)
    colors={'FUSELAGE':(228,224,200),'WING':(185,196,190),'TAIL':(20,97,101),
            'ENGINE':(226,224,205),'FAN':(22,31,36),'RUBBER':(27,30,32),
            'METAL':(141,159,162),'SHADOW':(16,25,30),'WHITE':(224,226,209),'TEAL':(19,100,104)}
    for key,box in REGIONS.items():d.rectangle(box,fill=colors[key]+(255,))
    if not blockout:
        # One continuous fuselage chart: no glass cards, painted door boxes,
        # floating logos, or coplanar trim. Both sides share the same paint.
        d.rectangle((2,2,254,18),fill=(242,239,215,255))
        d.rectangle((2,51,254,94),fill=(150,173,169,255))
        d.rectangle((2,44,254,48),fill=(16,79,87,255))
        d.rectangle((2,38,254,43),fill=(22,114,117,255))
        d.line((2,37,254,37),fill=(198,165,85,255),width=2)
        for x in range(66,202,8):
            d.rounded_rectangle((x,29,x+4,35),radius=1,fill=(11,33,45,255))
            d.line((x+1,30,x+3,30),fill=(102,143,153,255))
        # Cockpit glazing wraps around the tapered nose; dark centre frame.
        d.polygon([(218,28),(233,29),(246,38),(244,43),(219,36)],fill=(14,36,48,255))
        d.line((230,30,231,39),fill=(195,207,193,255),width=2)
        d.line((218,28,233,29),fill=(118,153,156,255))
        for x in (57,207):
            d.rounded_rectangle((x,25,x+7,52),radius=2,outline=(121,141,139,255))
            d.rectangle((x+2,29,x+4,34),fill=(13,36,47,255))
            d.line((x+2,44,x+5,44),fill=(81,103,107,255))
        # Cargo hatch seams and just a little service wear on the belly.
        for x in (81,168):d.rectangle((x,55,x+17,67),outline=(127,153,153,255))
        for x in (45,88,136,185):d.line((x,76,x+12,76),fill=(139,163,162,255))
        # Wing leading edge, flap tracks and access lines. No random noise.
        d.rectangle((2,98,126,102),fill=(223,226,210,255))
        d.line((4,148,124,148),fill=(102,124,129,255),width=2)
        d.line((4,151,124,151),fill=(215,218,202,255))
        for x in (34,68,98):d.line((x,145,x,166),fill=(93,119,124,255))
        # Fictional angular tail motif. No real manufacturer or airline mark.
        d.polygon([(136,165),(206,105),(242,105),(174,165)],fill=(235,228,194,255))
        d.polygon([(162,166),(225,114),(238,114),(177,166)],fill=(202,166,83,255))
        d.line((245,100,245,168),fill=(10,70,79,255),width=2)
        d.rectangle((2,199,126,204),fill=(22,105,111,255))
        d.line((2,197,126,197),fill=(198,164,84,255),width=2)
        d.line((20,175,20,212),fill=(165,178,169,255))
        d.line((113,175,113,212),fill=(165,178,169,255))
        # Recessed radial fan. The same physical closing face owns these UVs.
        cx,cy=152,196
        for j in range(12):
            a=j*math.pi/6
            points=[(cx+math.cos(a)*5,cy+math.sin(a)*5),
                    (cx+math.cos(a+.10)*17,cy+math.sin(a+.10)*17),
                    (cx+math.cos(a+.30)*17,cy+math.sin(a+.30)*17)]
            d.polygon(points,fill=(67,83,87,255))
        d.ellipse((147,191,157,201),fill=(101,114,113,255))
        d.ellipse((150,194,154,198),fill=(172,179,166,255))
        d.rectangle((219,179,251,184),fill=(202,209,197,255))
        # Wheel cap is ink on the side face of a separate tire mesh.
        d.ellipse((189,187,201,199),fill=(127,142,140,255))
        d.ellipse((193,191,197,195),fill=(51,69,73,255))
    TEXTURE.parent.mkdir(parents=True,exist_ok=True);im.save(TEXTURE)

def validate():
    allp=[];report={'shape':SHAPE,'entry_local':ENTRY,'pilot_local':PILOT,'gear':GEAR,'meshes':{}}
    for stem in ('body','gear','preview'):
        v,ids=read_mesh(MODEL/(stem+'.emesh'));p=v[:,:3];t=p[ids]
        assert np.isfinite(v).all() and ids.max()<len(v)
        assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all()
        assert (np.linalg.norm(np.cross(t[:,1]-t[:,0],t[:,2]-t[:,0]),axis=1)>1e-7).all()
        keys={tuple(q) for q in np.round(p,3)}
        assert all((-x,y,z) in keys for x,y,z in keys),'asymmetric geometry'
        lo,hi=p.min(0),p.max(0)
        report['meshes'][stem]={'triangles':len(ids),'bounds':[lo.tolist(),hi.tolist()]}
        if stem=='body':
            assert SHAPE['body_triangle_budget'][0]<=len(ids)<=SHAPE['body_triangle_budget'][1]
            assert np.allclose(hi-lo,[28,8.78,32],atol=.01)
            assert lo[1]>.40,'wheel geometry in airframe'
        if stem=='gear':assert abs(lo[1])<1e-5,'gear does not touch apron'
        allp.append((v,ids))
    assert report['meshes']['preview']['triangles']==sum(report['meshes'][k]['triangles'] for k in ('body','gear'))
    atlas=Image.open(TEXTURE);assert atlas.mode=='RGBA' and atlas.size==(256,256)
    guide=atlas.copy();d=ImageDraw.Draw(guide)
    for v,ids in allp[:2]:
        for tri in v[ids,6:8]:d.line([(u*256,(1-w)*256) for u,w in [*tri,tri[0]]],fill=(245,80,180,255),width=1)
    (ROOT/'build').mkdir(exist_ok=True)
    guide.save(ROOT/'build/aster-a80-uv.png')
    (ROOT/'build/aster-a80-fit-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--blockout',action='store_true');args=parser.parse_args()
    MODEL.mkdir(parents=True,exist_ok=True)
    texture(args.blockout)
    subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','--background','--python-exit-code','1','--python',str(ROOT/'tools/aster_a80_blender.py')],check=True)
    validate()
