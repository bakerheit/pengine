#!/usr/bin/env python3
"""Reproducible speedboat atlas, Blender source, cooked mesh and visual checks."""
import argparse
import json
import subprocess
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from marlin_sprint_spec import REGIONS,SHAPE,ENTRY,PILOT
from bake_vehicle_surfaces import read_mesh
from render_firetruck_preview import read_part,raster_view
ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/marlin_sprint'
TEXTURE=ROOT/'assets/textures/vehicles/marlin_sprint/body.png'

def texture(blockout):
    im=Image.new('RGBA',(256,256),(30,40,42,255));d=ImageDraw.Draw(im)
    colors={'HULL':(207,221,198),'DECK':(235,228,198),'GLASS':(37,77,86),
            'VINYL':(194,161,112),'TEAK':(120,77,45),'FLOOR':(84,118,113),
            'DASH':(27,43,46),'METAL':(152,171,167),'DARK':(25,38,40),
            'CREAM':(223,215,188),'SEAFOAM':(58,147,135),'RED':(190,43,32),
            'GREEN':(49,163,94)}
    for key,box in REGIONS.items():d.rectangle(box,fill=colors[key]+(255,))
    if not blockout:
        # Shared longitudinal hull chart: horizontal paint bands continue
        # across every chine/station. No overlaid body or decal geometry.
        d.rectangle((2,2,254,7),fill=(237,232,204,255))
        d.rectangle((2,16,254,24),fill=(70,158,144,255))
        d.line((2,15,254,15),fill=(33,82,81,255),width=2)
        d.line((2,25,254,25),fill=(194,160,94,255),width=2)
        d.rectangle((2,43,254,66),fill=(54,95,94,255))
        d.line((2,42,254,42),fill=(100,137,129,255),width=2)
        for x in (18,56,105,146):d.line((x,55,x+9,55),fill=(61,103,101,255),width=2)
        # Symmetric long bow highlight and inset anti-slip foredeck panel.
        d.polygon([(43,74),(85,74),(94,99),(88,113),(40,113),(34,99)],fill=(214,209,182,255))
        for y in range(80,112,5):d.line((44,y,84,y),fill=(227,221,192,255))
        d.line((63,72,63,116),fill=(247,239,211,255),width=2)
        # Broad stepped pixel reflections, never noise or modern sparkle.
        d.polygon([(132,76),(251,76),(251,83),(203,83),(203,87),(152,87),(152,91),(132,91)],fill=(98,147,155,255))
        d.rectangle((132,103,252,115),fill=(24,51,60,255))
        d.line((133,73,251,73),fill=(170,188,180,255))
        # Upholstery piping and small purposeful horizontal pleats.
        d.rectangle((133,125,202,174),outline=(128,100,71,255),width=2)
        d.line((136,128,199,128),fill=(232,200,148,255),width=2)
        for y in range(136,172,7):
            d.line((138,y,198,y),fill=(161,131,93,255))
            d.line((138,y+1,198,y+1),fill=(211,179,129,255))
        # Teak swim step and textured non-slip cockpit sole.
        for x in range(5,126,12):
            d.line((x,165,x,209),fill=(67,49,35,255),width=2)
            d.line((x+3,166,x+3,208),fill=(169,118,70,255))
        for y in range(185,225,6):
            d.line((133,y,203,y),fill=(70,103,100,255))
        # Instrument faces are on the actual angled dashboard panel.
        for x,y,r in ((222,144,8),(242,144,7),(222,166,4),(242,166,4)):
            d.ellipse((x-r,y-r,x+r,y+r),fill=(162,177,165,255))
            d.ellipse((x-r+2,y-r+2,x+r-2,y+r-2),fill=(17,29,32,255))
            d.line((x,y,x+2,y-4),fill=(225,204,151,255))
        d.rectangle((212,184,252,188),fill=(214,219,202,255))
        d.rectangle((212,201,252,204),fill=(84,109,111,255))
    TEXTURE.parent.mkdir(parents=True,exist_ok=True);im.save(TEXTURE)

def heights(triangles,x,z):
    result=[]
    for t in triangles:
        a,b,c=t[:,[0,2]];m=np.column_stack((b-a,c-a))
        if abs(np.linalg.det(m))<1e-8:continue
        u,v=np.linalg.solve(m,np.array([x,z])-a)
        if u>=-1e-6 and v>=-1e-6 and u+v<=1+1e-6:
            result.append(float(t[0,1]+u*(t[1,1]-t[0,1])+v*(t[2,1]-t[0,1])))
    return result

def validate():
    v,ids=read_mesh(MODEL/'body.emesh');p=v[:,:3];tri=p[ids]
    assert np.isfinite(v).all() and ids.max()<len(v)
    assert SHAPE['triangle_budget'][0]<=len(ids)<=SHAPE['triangle_budget'][1],len(ids)
    assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all()
    assert (np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)>1e-8).all()
    lo,hi=p.min(0),p.max(0)
    assert abs(hi[0]+lo[0])<.001
    assert 2.35<hi[0]-lo[0]<2.43
    assert np.allclose([lo[2],hi[2]],[-3.65,3.5],atol=.03)
    assert abs(lo[1]+.48)<.01 and 1.42<hi[1]<1.47
    centres=tri.mean(1);normals=v[ids,3:6].mean(1)
    uv=v[ids,6:8].mean(1)
    hull=(uv[:,1]>1-66/256)&(uv[:,1]<1-2/256)
    # Outboard side normals and upward deck normals are essential with the
    # real renderer's back-face culling (software preview draws both sides).
    sides=hull&(np.abs(centres[:,0])>.3)&(np.abs(normals[:,0])>.5)
    assert (centres[sides,0]*normals[sides,0]>0).all(),'inside-out hull'
    deck=(centres[:,2]>.8)&(centres[:,1]>.70)&(np.abs(centres[:,0])<.6)&(np.abs(normals[:,1])>.8)
    assert deck.any() and (normals[deck,1]>0).all(),'inside-out foredeck'
    # Preserve actual cavity AND foredeck: a solid hull cap or missing bow
    # must fail even if the side-view silhouette still looks plausible.
    for x,z in ((0,-1.6),(0,-.55),(-.44,-1.6),(.44,-1.6)):
        h=heights(tri,x,z);assert h and max(h)<.20,(x,z,h)
    for x,z in ((0,1.0),(-.4,1.4),(.4,1.4),(0,2.6)):
        h=heights(tri,x,z);assert h and max(h)>.70,(x,z,h)
    atlas=Image.open(TEXTURE);assert atlas.size==(256,256) and atlas.mode=='RGBA'
    guide=atlas.copy();d=ImageDraw.Draw(guide)
    for t in v[ids,6:8]:d.line([(u*256,(1-w)*256) for u,w in [*t,t[0]]],fill=(237,62,167,255))
    guide.save(ROOT/'build/marlin-sprint-uv.png')
    report=dict(shape=SHAPE,entry_local=ENTRY,pilot_local=PILOT,triangles=len(ids),vertices=len(v),
                bounds=[lo.tolist(),hi.tolist()],uv_range=[v[:,6:8].min(0).tolist(),v[:,6:8].max(0).tolist()],
                checks=['finite nondegenerate triangles','UV atlas bounds','open recessed cockpit',
                        'continuous foredeck','outward hull and upward deck normals',
                        'beam centered','no road wheels or rig'],
                integration='static moored asset; boating controls and boarding are separate work')
    (ROOT/'build/marlin-sprint-fit-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

def preview(suffix):
    part=read_part(MODEL/'body.emesh',TEXTURE)
    sheet=Image.new('RGB',(1200,880),(28,33,38));d=ImageDraw.Draw(sheet)
    for j,(yaw,pitch,label) in enumerate(((32,24,'BOW / COCKPIT'),(90,5,'STRICT SIDE'),(150,26,'STERN / SWIM STEP'),(0,53,'ELEVATED BOW'))):
        shot=raster_view([part],yaw,pitch,590,404)
        x=(j%2)*600;y=(j//2)*440;sheet.paste(shot,(x,y+25));d.text((x+18,y+9),label,fill=(224,228,216))
    sheet.save(ROOT/f'build/marlin-sprint-{suffix}.png')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--blockout',action='store_true');args=parser.parse_args()
    (ROOT/'build').mkdir(exist_ok=True);MODEL.mkdir(parents=True,exist_ok=True)
    texture(args.blockout)
    subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','--background','--python-exit-code','1','--python',str(ROOT/'tools/marlin_sprint_blender.py')],check=True)
    validate();preview('blockout' if args.blockout else 'preview')
