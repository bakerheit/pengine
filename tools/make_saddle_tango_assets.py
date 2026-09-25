#!/usr/bin/env python3
"""Cook Saddle Tango's 1991 burgundy atlas, source and runtime body."""
from __future__ import annotations

import hashlib
import json
import math
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from make_vesper_vx91_assets import read_emesh
from render_firetruck_preview import Part, raster_view, read_part, transformed
from saddle_tango_spec import MODEL, REFERENCE, REGIONS, ROOT, SHAPE, TEXTURE, WHEELS

BLENDER = Path('/Applications/Blender.app/Contents/MacOS/Blender')
PREVIEW = ROOT / 'build/saddle-tango-preview.png'
REPORT = ROOT / 'build/saddle-tango-fit-report.json'


def atlas():
    im=Image.new('RGBA',(256,256),(13,13,17,255))
    pixels=im.load()
    colors={
        'GLASS':(38,52,60), 'DARK':(14,15,18), 'METAL':(181,181,177),
        'HEADLIGHT':(218,224,211), 'AMBER':(231,138,38),
        'TAIL':(173,25,36), 'REVERSE':(213,219,208),
        'RUBBER':(20,20,24),
        'SILVER_DARK':(113,115,114), 'SEAM':(26,12,18),
    }
    for name,(x0,y0,x1,y1) in REGIONS.items():
        for y in range(y0,y1):
            v=(y-y0)/max(1,y1-y0-1)
            for x in range(x0,x1):
                if name=='BODY_SIDE':
                    # UV height runs upward in the atlas: dark rocker at bottom.
                    base=(77+int(46*v),17+int(13*v),31+int(16*v))
                    if v<.12:base=(48,17,27)
                elif name=='BODY_TOP':
                    base=(112+int(22*(1-v)),30+int(6*(1-v)),46+int(9*(1-v)))
                elif name=='BODY_END':
                    base=(91+int(26*v),24+int(7*v),38+int(8*v))
                elif name=='DOOR_CARD':
                    # Dark upper vinyl, burgundy cloth insert, and molded
                    # lower pocket. Both sides of every door use this cell.
                    if v<.25:base=(49,43,46)
                    elif v<.65:
                        weave=3 if (x+y)%4==0 else 0
                        base=(74+weave,46+weave,52+weave)
                    else:base=(44,42,45)
                    if y in (y0+13,y0+36):base=(114,100,99)
                elif name=='SEAT_FABRIC':
                    weave=5 if (x%3==0 and y%3==0) else 0
                    base=(82+weave,68+weave,70+weave)
                elif name=='HEADLINER':
                    base=(147+(x+y)%3,140+(x+y)%3,132+(x+y)%3)
                elif name=='CARPET':
                    base=(39+(x*3+y)%5,37+(x*3+y)%5,39+(x*3+y)%5)
                elif name=='DASHBOARD':
                    grain=2 if (x+y*2)%5==0 else 0
                    base=(38+grain,37+grain,40+grain)
                else:base=colors[name]
                pixels[x,y]=(*base,255)
    d=ImageDraw.Draw(im)
    for name in ('HEADLIGHT','AMBER','TAIL','REVERSE'):
        x0,y0,x1,y1=REGIONS[name]
        d.rectangle((x0+2,y0+2,x1-3,y1-3),outline=(68,68,66,255),width=1)
        for x in range(x0+5,x1-4,5):
            d.line((x,y0+4,x,y1-5),fill=(247,232,207,255) if name=='HEADLIGHT'
                   else (239,174,77,255) if name=='AMBER'
                   else (222,55,59,255) if name=='TAIL' else (238,239,228,255))
    TEXTURE.parent.mkdir(parents=True,exist_ok=True)
    im.save(TEXTURE)


def make_preview():
    body=read_part(MODEL/'body.emesh',TEXTURE)
    wheel=read_part(MODEL/'front_wheel.emesh',TEXTURE)
    native=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    scale=WHEELS['radius']/native
    parts=[body]
    for x in (-WHEELS['x'],WHEELS['x']):
        for y in (WHEELS['front_z'],WHEELS['rear_z']):
            parts.append(transformed(wheel,scale,(x,WHEELS['arch_y'],y)))
    views=[('FRONT 3/4',-32,13),('SIDE',-90,4),('REAR 3/4',-148,13)]
    out=Image.new('RGB',(1260,430),(21,23,28));d=ImageDraw.Draw(out)
    for i,(label,yaw,pitch) in enumerate(views):
        out.paste(raster_view(parts,yaw,pitch,420,390),(i*420,0))
        d.text((i*420+12,401),label,fill=(229,220,204))
    PREVIEW.parent.mkdir(parents=True,exist_ok=True)
    out.save(PREVIEW)


def validate():
    verts,ids=read_emesh(MODEL/'body.emesh')
    v=np.asarray(verts);tri=v[np.asarray(ids).reshape(-1,3),:3]
    assert np.isfinite(v).all() and len(tri)>300
    assert len(ids)%3==0 and min(ids)>=0 and max(ids)<len(v)
    assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all()
    areas=np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)
    assert (areas>1e-9).all(),f'{(areas<=1e-9).sum()} degenerate triangles'
    lo,hi=v[:,:3].min(0),v[:,:3].max(0)
    assert 4.85 < hi[2]-lo[2] < 5.1,(lo,hi)
    assert 1.8 < hi[0]-lo[0] < 2.1,(lo,hi)
    assert 1.4 < hi[1] < 1.55 and .15 < lo[1] < .25,(lo,hi)
    # Both exterior side walls have open wheel centres; the midbody retains a floor.
    for axle in (WHEELS['front_z'],WHEELS['rear_z']):
        for side in (-1,1):
            # Inner wheelhouse liners deliberately cover the cabin side;
            # the exterior wheel aperture must still be open.
            outer=tri[(side*tri[:,:,0]>.82).all(1)]
            center=np.array([axle,WHEELS['arch_y']])
            for t in outer:
                a,b,c=t[:,[2,1]]
                cross=lambda u,w:(u[0]*w[1]-u[1]*w[0])
                signs=[cross(b-a,center-a),cross(c-b,center-b),
                       cross(a-c,center-c)]
                assert not (min(signs)>1e-7 or max(signs)<-1e-7),\
                    f'closed side opening {axle} {side}'
    with Image.open(TEXTURE) as im:
        assert im.size==(256,256) and im.mode=='RGBA'
    wheel_vertices,wheel_ids=read_emesh(MODEL/'front_wheel.emesh')
    wheel_array=np.asarray(wheel_vertices)
    assert np.isfinite(wheel_array).all() and len(wheel_ids)%3==0
    assert .345 < max(np.ptp(wheel_array[:,1]),np.ptp(wheel_array[:,2]))*.5 < .355
    assert .20 < np.ptp(wheel_array[:,0]) < .25
    assert (MODEL/'front_wheel.emesh').read_bytes()==(MODEL/'rear_wheel.emesh').read_bytes()
    interior_counts={}
    for name in ('DOOR_CARD','SEAT_FABRIC','DASHBOARD','HEADLINER','CARPET'):
        x0,y0,x1,y1=REGIONS[name]
        interior_counts[name]=int(((v[:,6]>=x0/256)&(v[:,6]<=x1/256)&
                                   (v[:,7]>=1-y1/256)&(v[:,7]<=1-y0/256)).sum())
        assert interior_counts[name]>20,(name,interior_counts[name])
    glass_triangles={}
    for name in ('windshield','rear_glass','passenger_glass','driver_glass',
                 'driver_rear_glass','passenger_rear_glass'):
        pane,pane_ids=read_emesh(MODEL/(name+'.emesh'))
        pane=np.asarray(pane)
        assert len(pane_ids)>=6 and len(pane_ids)%3==0 and np.isfinite(pane).all()
        glass_triangles[name]=len(pane_ids)//3
    report={
        'name':'Saddle Tango','year':1991,'shape':SHAPE,'wheels':WHEELS,
        'reference_directory':str(REFERENCE.relative_to(ROOT)),
        'vertices':len(v),'triangles':len(tri),
        'wheel_triangles':len(wheel_ids)//3,
        'glass_triangles':glass_triangles,
        'textured_interior_vertices':interior_counts,
        'bounds_min':lo.tolist(),'bounds_max':hi.tolist(),
        'mesh_sha256':hashlib.sha256((MODEL/'body.emesh').read_bytes()).hexdigest(),
        'atlas_sha256':hashlib.sha256(TEXTURE.read_bytes()).hexdigest(),
        'checks':['finite indexed mesh','nondegenerate triangles',
                  'UVs in atlas','1991 sedan bounds','four side wheel openings',
                  '256x256 RGBA atlas','matched basketweave wheel pair',
                  'six separate translucent pane meshes',
                  'textured inward door skins and cabin surfaces'],
    }
    REPORT.parent.mkdir(parents=True,exist_ok=True)
    REPORT.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'triangles':len(tri),'bounds':(hi-lo).tolist(),
                      'preview':str(PREVIEW)},indent=2))


def main():
    atlas()
    subprocess.run([str(BLENDER),'-b','--python-exit-code','1','--python',
                    str(ROOT/'tools/saddle_tango_blender.py')],check=True)
    validate()
    make_preview()


if __name__=='__main__':main()
