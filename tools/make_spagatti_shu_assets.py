#!/usr/bin/env python3
"""Reproducible Shū cook, limited-color atlas and cooked-mesh regression checks."""
from __future__ import annotations
import argparse,json,math,subprocess
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from bake_vehicle_surfaces import read_mesh
from spagatti_shu_spec import ATLAS_SIZE,REGIONS,UV_PROJECTIONS,SHAPE,WHEEL_ANCHORS,PLATE_MOUNTS,LAMPS
ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/spagatti_shu'
TEXTURE=ROOT/'assets/textures/vehicles/spagatti_shu/body.png'
PANES=('windshield','rear_glass','passenger_glass','driver_glass','driver_rear_glass','passenger_rear_glass')
COLORS={'BODY_SIDE':(155,29,34),'BODY_TOP':(155,29,34),'BODY_FRONT':(155,29,34),'BODY_REAR':(155,29,34),
 'GLASS_SIDE':(43,60,66),'GLASS_FRONT':(43,60,66),'GLASS_REAR':(43,60,66),'CLADDING':(23,24,26),
 'BODY_SHADOW':(90,18,24),'SEAM':(124,98,65),'METAL':(143,147,143),'BLACK':(12,15,17),
 'HEADLIGHT':(199,203,184),'TAIL_RED':(164,28,31),'EXHAUST':(37,39,41),'LENS_DARK':(23,26,29)}

def pixel(key,x,y):
    _,((a,b),(c,d))=UV_PROJECTIONS[key];x0,y0,x1,y1=REGIONS[key]
    return (round(x0+2+(x-a)/(b-a)*(x1-x0-4)),round(y1-2-(y-c)/(d-c)*(y1-y0-4)))

def texture(clay=False):
    im=Image.new('RGBA',(256,256),(12,15,17,255));d=ImageDraw.Draw(im)
    for key,box in REGIONS.items():d.rectangle(box,fill=(*((145,146,141) if clay and key.startswith('BODY_') else COLORS[key]),255))
    if clay:return im
    def poly(key,points,fill):d.polygon([pixel(key,*p) for p in points],fill=(*fill,255))
    def ellipse(key,x,y,r,color):
        a=pixel(key,x-r,y-r);b=pixel(key,x+r,y+r)
        d.ellipse((min(a[0],b[0]),min(a[1],b[1]),max(a[0],b[0]),max(a[1],b[1])),fill=(*color,255))
    # Broad shared projection: quiet pressed panels with a narrow lower shadow.
    poly('BODY_SIDE',[(-2.42,.16),(2.42,.16),(2.42,.30),(-2.42,.30)],(99,21,26))
    # Door shut line follows this coupe's long front-hinged door.
    seam=[(.62,.78),(.67,.61),(.62,.37),(-.45,.37),(-.57,.64),(-.57,.78)]
    d.line([pixel('BODY_SIDE',*p) for p in seam],fill=(79,18,22,255),width=1)
    d.line([pixel('BODY_SIDE',-.31,.745),pixel('BODY_SIDE',-.10,.745)],fill=(183,157,112,255),width=1)
    poly('BODY_REAR',[(-.98,.43),(.98,.43),(.95,.69),(-.95,.69)],(22,24,26))
    for side in (-1,1):
        for x in (.60,.86):
            ellipse('BODY_REAR',side*x,.56,.099,(57,12,16))
            ellipse('BODY_REAR',side*x,.56,.074,(156,26,30))
            ellipse('BODY_REAR',side*x,.58,.031,(193,46,39))
        poly('BODY_FRONT',[(side*.64,.535),(side*.87,.535),(side*.87,.565),(side*.64,.565)],(76,69,49))
        poly('BODY_FRONT',[(side*.67,.54),(side*.84,.54),(side*.84,.56),(side*.67,.56)],(164,142,75))
        poly('BODY_FRONT',[(side*.44,.32),(side*.90,.32),(side*.88,.43),(side*.46,.43)],(22,24,26))
    # The dedicated receiver gives each geometric lamp a reflector and lens.
    x0,y0,x1,y1=REGIONS['HEADLIGHT']
    d.ellipse((x0+2,y0+2,x1-2,y1-2),fill=(92,104,106,255))
    d.ellipse((x0+6,y0+4,x1-6,y1-4),fill=(192,198,186,255))
    d.ellipse((x0+13,y0+8,x1-13,y1-8),fill=(106,124,124,255))
    # Tiny bonnet badge. Geometry and the grille silhouette do the heavy lifting.
    d.line([pixel('BODY_TOP',-.045,2.15),pixel('BODY_TOP',.045,2.15)],fill=(186,165,123,255),width=1)
    # Restrained metal gradient, no white rubber reflections.
    x0,y0,x1,y1=REGIONS['METAL']
    for y,col in [(y0+4,(100,106,105)),(y0+8,(176,180,169)),(y0+11,(147,153,148))]:d.rectangle((x0+2,y,x1-2,y+2),fill=(*col,255))
    return im


def contains(point,triangle):
    a,b,c=np.asarray(triangle,float);v0=b-a;v1=c-a;v2=np.asarray(point)-a
    det=v0[0]*v1[1]-v0[1]*v1[0]
    if abs(det)<1e-10:return False
    u=(v2[0]*v1[1]-v2[1]*v1[0])/det;v=(v0[0]*v2[1]-v0[1]*v2[0])/det
    return u>1e-6 and v>1e-6 and u+v<1-1e-6


def validate(model=MODEL,tex=TEXTURE):
    v,idx=read_mesh(model/'body.emesh');v=np.asarray(v);idx=np.asarray(idx);points=v[:,:3];tris=points[idx]
    assert SHAPE['triangle_budget'][0]<=len(idx)<=SHAPE['triangle_budget'][1],('body triangle budget',len(idx))
    assert np.isfinite(v).all(),'non-finite mesh'
    assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all(),'UV outside atlas'
    lo=points.min(axis=0);hi=points.max(axis=0);size=hi-lo
    assert np.all(np.abs(size-[2.17,1.065,4.82])< [.06,.035,.05]),('body bounds',size)
    assert abs(lo[0]+hi[0])<.005 and abs(lo[2]+hi[2])<.03,'asymmetric body'
    assert lo[1]>=.15,'wheel or ground-cutting geometry in body'
    # Preserve both parts of the rounded front: a crowned hood and a fascia
    # that wraps back at the corners instead of ending in a broad flat wall.
    shape_samples=[]
    for point,axes,depth in [((.32,.50),(0,1),2),((.92,.50),(0,1),2),((.13,1.75),(0,2),1)]:
        hits=[]
        for t in tris:
            a,b,c=t[:,axes];delta=np.column_stack((b-a,c-a))
            if abs(np.linalg.det(delta))<1e-9:continue
            u,w=np.linalg.solve(delta,np.asarray(point)-a)
            if u>=-1e-6 and w>=-1e-6 and u+w<=1+1e-6:
                hits.append((1-u-w)*t[0,depth]+u*t[1,depth]+w*t[2,depth])
        assert hits,('missing front surface',point)
        shape_samples.append(max(hits))
    assert .27<shape_samples[0]-shape_samples[1]<.36,('front corners lost their sweep',shape_samples)
    assert .90<shape_samples[2]<.95,('hood lost its crown',shape_samples[2])
    # Wheel centers are real openings; the hood and rear deck must survive.
    for z in (1.45,-1.35):
        for s in (-1,1):
            for dz,dy in [(0,0),(.09,0),(-.09,0),(0,.12)]:
                assert not any(np.all(t[:,0]*s>.70) and contains((z+dz,.43+dy),t[:,[2,1]]) for t in tris),('wheel opening blocked',s,z,dz,dy)
        for x in (-.52,0,.52):
            for dz in (-.06,.06):
                assert any(np.all(t[:,1]>.63) and contains((x,z+dz),t[:,[0,2]]) for t in tris),('hood/deck missing',x,z+dz)
    cooked=json.loads((model/'cook_report.json').read_text())
    assert cooked['wheel_fit']['wheel_intrusions']==0 and cooked['wheel_fit']['wheel_samples']>=1000,'steering clearance failed'
    # Every visible lamp sample must hit its lens atlas cell first. Counting
    # lens triangles alone would let an intact fender bury the outer pair.
    lamp_samples=0
    for cx,cy in LAMPS['headlights']['centers']:
        for dx,dy in [(0,0),(-.035,0),(.035,0),(0,-.035),(0,.035)]:
            hits=[]
            for triangle,indices in zip(tris,idx):
                a,b,c=triangle[:,:2];delta=np.column_stack((b-a,c-a))
                if abs(np.linalg.det(delta))<1e-9:continue
                u,w=np.linalg.solve(delta,np.array([cx+dx,cy+dy])-a)
                if u>=-1e-6 and w>=-1e-6 and u+w<=1+1e-6:
                    bary=np.array([1-u-w,u,w]);hits.append((float(bary@triangle[:,2]),bary@v[indices,6:8]))
            assert hits,('headlamp has no receiver',cx,cy,dx,dy)
            depth,uv=max(hits,key=lambda hit:hit[0]);px,py=uv*[256,-256]+[0,256]
            x0,y0,x1,y1=REGIONS['HEADLIGHT']
            assert x0<px<x1 and y0<py<y1,('buried or missing headlamp',cx,cy,dx,dy,px,py)
            assert LAMPS['headlights']['z'][0]<depth<LAMPS['headlights']['z'][1],('headlamp outside beam receiver',depth)
            lamp_samples+=1
    lens_uvs={};x0,y0,x1,y1=REGIONS['HEADLIGHT']
    for row in v:
        px,py=row[6:8]*[256,-256]+[0,256]
        if x0<px<x1 and y0<py<y1:
            position=tuple(np.round(row[:3],5))
            lens_uvs.setdefault(position,set()).add(tuple(np.round(row[6:8],5)))
    assert lens_uvs and all(len(uvs)==1 for uvs in lens_uvs.values()),'fragmented headlamp UVs'
    # All 18 registration samples sit just ahead of the recessed backing.
    for end in ('front','rear'):
        cx,cy,cz=PLATE_MOUNTS[end]['center'];sign=1 if end=='front' else -1
        for dx in (-.5,0,.5):
            for dy in (-.5,0,.5):
                x=cx+dx*PLATE_MOUNTS['width'];y=cy+dy*PLATE_MOUNTS['height'];hits=[]
                for t in tris:
                    a,b,c=t[:,:2];delta=np.column_stack((b-a,c-a));det=np.linalg.det(delta)
                    if abs(det)<1e-9:continue
                    u,w=np.linalg.solve(delta,np.array([x,y])-a)
                    if u>=-1e-6 and w>=-1e-6 and u+w<=1+1e-6:hits.append(t[0,2]+u*(t[1,2]-t[0,2])+w*(t[2,2]-t[0,2]))
                assert hits,('plate has no backing',end,dx,dy)
                clearance=sign*(cz-(max(hits) if sign>0 else min(hits)))
                assert .001<clearance<.008,('plate intersects or floats',end,clearance)
    counts={}
    for name in PANES:
        p,i=read_mesh(model/(name+'.emesh'));p=np.asarray(p);i=np.asarray(i);counts[name]=len(i)
        assert len(i)>=12 and np.isfinite(p).all(),('missing/invalid glazing',name)
        assert np.ptp(p[:,1])>.12,('collapsed pane',name)
        # No glass material can leak back into the opaque body.
    for key in ('GLASS_FRONT','GLASS_REAR','GLASS_SIDE'):
        a,b,c,d=REGIONS[key];uv=v[:,6:8]*[256,-256]+[0,256]
        assert not np.any(np.all((uv[idx,:,] > [a+1,b+1]) & (uv[idx,:,] < [c-1,d-1]),axis=(1,2))),('glass baked into opaque body',key)
    for side in (-1,1):
        for sample in [(0.0,1.02),(-.77,.98)]:
            assert not any(np.all(t[:,0]*side>.48) and contains(sample,t[:,[2,1]]) for t in tris),('opaque side window',side,sample)
    # Ray through the center windshield must reach the open cabin, not red paint.
    assert not any(np.all((t[:,2]>.32)&(t[:,2]<.90)) and contains((.1,1.02),t[:,[0,1]]) for t in tris),'opaque windshield backing'
    im=Image.open(tex);assert im.size==(256,256) and im.mode=='RGBA','atlas format'
    assert im.getchannel('A').getextrema()==(255,255),'opaque atlas; glazing uses runtime shader'
    palette=len(set(im.getdata()));assert palette<=96,('palette',palette)
    # Actual end receivers contain enough separate light/dark texels to read.
    for key in ('BODY_REAR',):
        crop=np.asarray(im.crop(REGIONS[key]));assert len(np.unique(crop.reshape(-1,4),axis=0))>=5,('missing lamps',key)
    report={'asset':SHAPE['name'],'triangles':len(idx),'vertices':len(v),'bounds_min':lo.tolist(),'bounds_max':hi.tolist(),'bounds_size':size.tolist(),'atlas':[256,256,'RGBA'],'palette_colors':palette,'glass_triangles':counts,'headlamp_visibility_samples':lamp_samples,'front_corner_sweep':float(shape_samples[0]-shape_samples[1]),'hood_crown_height':float(shape_samples[2]),'wheel_anchors':WHEEL_ANCHORS,'wheel_fit':cooked['wheel_fit'],'plate_mounts':PLATE_MOUNTS,'checks':['rounded front corner sweep and hood crown','real four wheel openings','20 visible recessed headlamp samples','18 plate-backing clearance samples','center hood and deck retained','connected manifold roof and surrounds (Blender cook)','six separate panes and clear window apertures','opaque body excludes glass','1,600-triangle reference-pass body budget and shared wheel rig']}
    out=ROOT/'build/spagatti-shu-fit-report.json';out.write_text(json.dumps(report,indent=2)+'\n')
    guide=im.resize((1024,1024),Image.Resampling.NEAREST);d=ImageDraw.Draw(guide)
    for t in idx:
        uv=[(v[j,6]*1024,(1-v[j,7])*1024) for j in t];d.line(uv+[uv[0]],fill=(50,240,181,255),width=1)
    guide.save(ROOT/'build/spagatti-shu-uv-guide.png')
    print(json.dumps(report,indent=2));return report


def main():
    p=argparse.ArgumentParser();p.add_argument('--validate-only',action='store_true');p.add_argument('--texture-only',action='store_true');p.add_argument('--preview-only',action='store_true');p.add_argument('--blockout',action='store_true');a=p.parse_args()
    if not(a.validate_only or a.preview_only):TEXTURE.parent.mkdir(parents=True,exist_ok=True);texture(a.blockout).save(TEXTURE)
    if not(a.validate_only or a.texture_only or a.preview_only):
        command=['/Applications/Blender.app/Contents/MacOS/Blender','--background','--factory-startup','--python-exit-code','1','--python',str(ROOT/'tools/spagatti_shu_blender.py'),'--','--mesh',str(MODEL/'body.emesh'),'--blend',str(MODEL/'source.blend')]
        if a.blockout:command.append('--blockout')
        subprocess.run(command,cwd=ROOT,check=True)
    if not a.blockout:validate()
    subprocess.run(['python3',str(ROOT/'tools/preview_spagatti_shu.py')],cwd=ROOT,check=True)
if __name__=='__main__':main()
