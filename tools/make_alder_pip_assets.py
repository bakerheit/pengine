#!/usr/bin/env python3
"""Cook the full-size ALDER PIP compact hatchback and validate shared-wheel clearance.

The legacy closed mesh retains painted glass. The articulated player model
uses capped opaque frames and six separate panes for the runtime glass pass.
Seams and lamps remain painted directly onto their final receiver surfaces.
"""
import argparse
import hashlib
import json
import math
import struct
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from alder_pip_spec import ATLAS_SIZE, REGIONS, BOUNDS, WHEELS, SHAPE, LAMPS, DOOR, DRIVER
from make_vesper_vx91_assets import read_emesh
from render_firetruck_preview import Part, read_part, raster_view
from validate_vehicle_doors import validate_and_preview

ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/alder_pip'
TEXTURE=ROOT/'assets/textures/vehicles/alder_pip/body.png'
PREFIX=ROOT/'build/alder_pip'
SCALE=np.ones(3)


def output(suffix):
    return Path(str(PREFIX)+suffix)


def make_texture(blockout=False):
    paint=(191,160,79,255);light=(210,183,112,255);dark=(29,33,32,255)
    glass=(45,66,70,255);silver=(158,165,156,255)
    im=Image.new('RGBA',(256,256),dark);d=ImageDraw.Draw(im)
    for name,box in REGIONS.items():
        d.rectangle(box,fill=dark if name in ('BLACK','SHADOW','CLADDING') else silver if name=='METAL' else light if name=='TOP' else paint)
    def point(name,p):
        (a,b),(c,e)=BOUNDS[name];x0,y0,x1,y1=REGIONS[name]
        return (round(x0+2+(p[0]-a)/(b-a)*(x1-x0-4)),round(y1-2-(p[1]-c)/(e-c)*(y1-y0-4)))
    def rect(name,a,b,c,e,color):
        pts=[point(name,(a,b)),point(name,(c,e))]
        d.rectangle((min(pts[0][0],pts[1][0]),min(pts[0][1],pts[1][1]),max(pts[0][0],pts[1][0]),max(pts[0][1],pts[1][1])),fill=color)
    def line(name,pts,color,width=1):d.line([point(name,p) for p in pts],fill=color,width=width)
    def poly(name,pts,color):d.polygon([point(name,p) for p in pts],fill=color)
    if not blockout:
        rect('SIDE',-1.82,.19,1.82,.28,(79,75,53,255))
        line('SIDE',[(-1.80,.50),(1.80,.50)],dark,2)
        line('SIDE',[(-.55,.51),(-.55,.84),(.58,.84),(.58,.46)],(111,95,51,255))
        rect('SIDE',-.50,.755,-.28,.80,dark)
        rect('SIDE',1.44,.56,1.62,.64,(204,126,51,255))
        # Long front door and short rear quarter; pillars follow the actual cabin rake.
        poly('CABIN',[(-1.73,.93),(-1.49,1.43),(-.59,1.43),(-.59,.93)],dark)
        poly('CABIN',[(-1.60,.98),(-1.44,1.36),(-.66,1.36),(-.66,.98)],glass)
        poly('CABIN',[(-.53,.93),(-.53,1.43),(.13,1.43),(.56,.93)],dark)
        poly('CABIN',[(-.45,.98),(-.45,1.36),(.10,1.36),(.43,.98)],glass)
        line('CABIN',[(-1.43,1.30),(-.73,1.03)],(85,108,108,255),2)
        line('CABIN',[(-.36,1.30),(.27,1.04)],(85,108,108,255),2)
        for name in ('WINDSHIELD','BACK_GLASS'):
            poly(name,[(-.75,.92),(-.65,1.44),(.65,1.44),(.75,.92)],dark)
            poly(name,[(-.66,.98),(-.59,1.38),(.59,1.38),(.66,.98)],glass)
            line(name,[(-.47,1.31),(.45,1.06)],(85,108,108,255),2)
        line('WINDSHIELD',[(-.60,.965),(-.18,1.03)],dark)
        line('WINDSHIELD',[(.05,.965),(.47,1.03)],dark)
        line('BACK_GLASS',[(-.24,.96),(.24,1.08)],dark)
        rect('FRONT',-.76,.51,.76,.75,dark)
        for side in (-1,1):
            a,c=sorted((side*.40,side*.74))
            rect('FRONT',a,.565,c,.72,(232,227,201,255))
            a,c=sorted((side*.66,side*.75))
            rect('FRONT',a,.48,c,.535,(214,135,50,255))
        for y in (.555,.615,.675):line('FRONT',[(-.32,y),(.32,y)],(79,82,73,255))
        rect('FRONT',-.23,.27,.23,.38,(193,201,175,255))
        for side in (-1,1):
            a,c=sorted((side*.56,side*.77))
            rect('REAR',a,.46,c,.81,dark)
            rect('REAR',a+.025,.60,c-.025,.77,(167,45,35,255))
            rect('REAR',a+.025,.48,c-.025,.56,(215,147,54,255))
        rect('REAR',-.27,.54,.27,.68,dark)
        rect('REAR',-.22,.565,.22,.65,(202,207,179,255))
        line('REAR',[(-.48,.83),(.48,.83)],(117,102,60,255))
        for x in (-.55,.55):line('TOP',[(x,.73),(x,1.72)],(176,145,65,255))
        for x in (-.65,.65):line('TOP',[(x,-1.50),(x,.10)],(192,163,87,255))
    TEXTURE.parent.mkdir(parents=True,exist_ok=True);im.save(TEXTURE)


def contains(p,t):
    a,b,c=t
    cross=lambda x,y:x[0]*y[1]-x[1]*y[0]
    values=[cross(b-a,p-a),cross(c-b,p-b),cross(a-c,p-c)]
    return min(values)>1e-7 or max(values)<-1e-7


def wheel_part():
    return read_part(ROOT/'assets/models/vehicles/common/wheel.emesh',ROOT/'assets/textures/vehicles/common/wheel.png')


def validate(blockout=False):
    vertices,indices=read_emesh(MODEL/'body.emesh')
    v=np.asarray(vertices)
    assert len(indices)%3==0 and len(indices)>0
    assert np.isfinite(v).all() and min(indices)>=0 and max(indices)<len(v)
    tris=v[np.asarray(indices).reshape(-1,3),:3]
    areas=np.linalg.norm(np.cross(tris[:,1]-tris[:,0],tris[:,2]-tris[:,0]),axis=1)
    assert (areas>1e-9).all(),'degenerate triangles'
    assert SHAPE['triangle_budget'][0]<=len(tris)<=SHAPE['triangle_budget'][1],len(tris)
    assert ((v[:,6:8]>=0)&(v[:,6:8]<=1)).all()
    lo,hi=v[:,:3].min(0),v[:,:3].max(0)
    assert np.allclose(hi-lo,[1.94,1.33,3.80],atol=.001),(lo,hi)
    keys={tuple(np.round(p,4)) for p in v[:,:3]}
    assert all((-x,y,z) in keys for x,y,z in keys),'asymmetric body'
    for axle in (WHEELS['front_z'],WHEELS['rear_z']):
        for s in (-1,1):
            outer=tris[(s*tris[:,:,0]>.37).all(1)]
            for dz,dy in ((.013,.017),(.15,.017),(-.15,.017),(.013,.16),(.013,-.14)):
                p=np.array([axle+dz,WHEELS['arch_y']+dy])
                assert not any(contains(p,t[:,[2,1]]) for t in outer),'closed wheel opening'
    # Preserve a broad hood and complete hatch/cabin roof across both axles.
    for x in (-.63,-.23,.023,.31,.64):
        for z in (-1.4,-.7,.03,.8,1.11,1.62):
            assert any(contains(np.array([x,z]),t[:,[0,2]]) for t in tris if (t[:,1]>.735).all()),'missing hood/cabin coverage'
    for x in (-.21,.23):
        for z in (-.81,.31,.93):
            assert any(contains(np.array([x,z]),t[:,[0,2]]) for t in tris if (t[:,1]<.33).all()),'missing central chassis'
    # Exact shared-wheel measured bounds set the finite tire-cylinder envelope.
    wheel=wheel_part()
    radius=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    wheel_scale=WHEELS['radius']/radius
    halfwidth=np.max(np.abs(wheel.positions[:,0]))*wheel_scale
    scaled=tris*SCALE
    cloud=[]
    for t in scaled:
        if t[:,1].min()>WHEELS['arch_y']*SCALE[1]+WHEELS['radius']+.004:continue
        n=max(1,math.ceil(max(np.linalg.norm(t[i]-t[j]) for i,j in ((0,1),(1,2),(2,0)))/.02))
        for i in range(n+1):
            js=np.arange(n+1-i)[:,None]/n
            cloud.append(t[0]+i/n*(t[1]-t[0])+js*(t[2]-t[0]))
    cloud=np.concatenate(cloud)
    poses=0
    for axle in (WHEELS['front_z'],WHEELS['rear_z']):
        for side in (-1,1):
            rel=cloud-np.array([side*WHEELS['x'],WHEELS['arch_y']*SCALE[1],axle*SCALE[2]])
            for angle in (np.linspace(-.82,.82,33) if axle>0 else [0]):
                width=rel[:,0]*math.cos(angle)+rel[:,2]*math.sin(angle)
                radial=-rel[:,0]*math.sin(angle)+rel[:,2]*math.cos(angle)
                bad=(np.abs(width)<halfwidth+.002)&(radial**2+rel[:,1]**2<(WHEELS['radius']+.002)**2)
                assert not bad.any(),f'tire contact: {axle}, {side}, {angle}, {(cloud[bad][:5]/SCALE).tolist()}'
                poses+=1
    with Image.open(TEXTURE) as atlas:
        assert atlas.size==(256,256) and atlas.mode=='RGBA'
        assert atlas.getchannel('A').getextrema()==(255,255)
    glass_names=('windshield','rear_glass','passenger_glass','driver_glass',
                 'driver_rear_glass','passenger_rear_glass')
    for name in glass_names:
        pane_vertices,pane_indices=read_emesh(MODEL/(name+'.emesh'))
        pane=np.asarray(pane_vertices)
        assert len(pane_indices)==6 and np.isfinite(pane).all(),name
        assert ((pane[:,6:8]>=0)&(pane[:,6:8]<=1)).all(),name
        faces=pane[np.asarray(pane_indices).reshape(-1,3),:3]
        assert (np.linalg.norm(np.cross(faces[:,1]-faces[:,0],faces[:,2]-faces[:,0]),axis=1)>.01).all(),name
    # Check safe lamp masks against actual cooked UVs so future light profiles
    # illuminate only their correct receiver pixels, never body paint or trim.
    rgba=np.asarray(Image.open(TEXTURE))
    all_tri=v[np.asarray(indices).reshape(-1,3)]
    lamp_samples=0
    for name,(x0,y0,x1,y1,z) in LAMPS.items():
        faces=all_tri[(np.abs(all_tri[:,:,2]-z)<1e-5).all(1)]
        for side in (-1,1):
            for x in np.linspace(x0+.0003,x1-.0002,7):
                for y in np.linspace(y0+.0003,y1-.0002,7):
                    p=np.array([side*x,y]);hits=[t for t in faces if contains(p,t[:,[0,1]])]
                    assert len(hits)==1,(name,'receiver coverage',p,len(hits))
                    t=hits[0];weights=np.linalg.solve(np.vstack([t[:,:2].T,np.ones(3)]),np.array([*p,1]))
                    uv=weights@t[:,6:8]
                    pixel=rgba[min(255,int((1-uv[1])*256)),min(255,int(uv[0]*256)),:3]
                    if not blockout:
                        if name=='rear_red':assert pixel[0]>120 and pixel[1]<80 and pixel[2]<80,(name,pixel,p)
                        else:assert pixel.min()>170,(name,pixel,p)
                    lamp_samples+=1
    report=dict(model='alder_pip',vertices=len(v),triangles=len(tris),bounds_min=lo.tolist(),bounds_max=hi.tolist(),
                dimensions=(hi-lo).tolist(),shape_contract=SHAPE,wheels=WHEELS,
                lamp_receiver_samples=lamp_samples,player_fit_scale=SCALE.tolist(),fitted_dimensions=((hi-lo)*SCALE).tolist(),
                wheel_native_radius=float(radius),wheel_native_halfwidth=float(halfwidth/wheel_scale),
                fitted_wheel_radius=WHEELS['radius'],fitted_wheel_halfwidth=float(halfwidth),
                articulated_glass=list(glass_names),
                atlas=[256,256,'RGBA'],uv_min=v[:,6:8].min(0).tolist(),uv_max=v[:,6:8].max(0).tolist(),
                checks=['finite static mesh, valid indices, no degenerate faces','triangle budget','symmetric geometry and measured bounds',
                        'four real open side pockets','continuous hood/cabin roof and central chassis','direct receiver atlas; no floating glass/lamp/seam skins',
                        'four anchor empties; one joined body; no wheels in exported source','opaque 256x256 RGBA atlas',
                        'sampled shared-tire envelope clear through full steering range',
                        'headlamp and rear red masks match cooked receiver pixels',
                        'six independent single-layer articulated glass panes'],
                steering_test=dict(surface_samples=len(cloud),max_surface_spacing=.02,poses=poses,max_lock=.82),
                mesh_sha256=hashlib.sha256((MODEL/'body.emesh').read_bytes()).hexdigest(),
                atlas_sha256=hashlib.sha256(TEXTURE.read_bytes()).hexdigest())
    output('-fit-report.json').write_text(json.dumps(report,indent=2)+'\n')
    guide=Image.open(TEXTURE).copy()
    d=ImageDraw.Draw(guide)
    for ids in np.asarray(indices).reshape(-1,3):
        pts=[(v[i,6]*256,(1-v[i,7])*256) for i in ids]
        d.line(pts+[pts[0]],fill=(240,151,52,255))
    guide.save(output('-uv-guide.png'))
    print(json.dumps(report,indent=2))
    return report


def parts(steer=0):
    body=read_part(MODEL/'body.emesh',TEXTURE)
    body.positions*=SCALE
    body.normals/=SCALE
    body.normals/=np.linalg.norm(body.normals,axis=1)[:,None]
    result=[body]
    wheel=wheel_part()
    radius=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    for axle in (WHEELS['front_z'],WHEELS['rear_z']):
        for side in (-1,1):
            angle=steer if axle>0 else 0
            c,s=math.cos(angle),math.sin(angle)
            rot=np.array([[c,0,-s],[0,1,0],[s,0,c]])
            p=(wheel.positions*(WHEELS['radius']/radius))@rot.T
            p+=np.array([side*WHEELS['x'],WHEELS['arch_y']*SCALE[1],axle*SCALE[2]])
            result.append(Part(p,wheel.normals@rot.T,wheel.uvs,wheel.indices,wheel.texture))
    return result


def previews(blockout=False):
    straight,locked=parts(),parts(.82)
    views=[('FRONT 3/4',straight,-32,18),('SIDE',straight,-90,0),('REAR 3/4',straight,-148,18),
           ('ELEVATED FRONT',straight,-32,38),('FULL LOCK',locked,-32,25),('REAR',straight,180,0)]
    sheet=Image.new('RGB',(1440,840),(20,23,26))
    d=ImageDraw.Draw(sheet)
    for i,(label,items,yaw,pitch) in enumerate(views):
        image=raster_view(items,yaw,pitch,480,386)
        image.save(output('-'+label.lower().replace(' ','-').replace('/','')+('.blockout' if blockout else '')+'.png'))
        x,y=(i%3)*480,(i//3)*420
        sheet.paste(image,(x,y))
        d.text((x+12,y+397),label,fill=(230,230,215))
    sheet.save(output('-blockout.png' if blockout else '-preview.png'))
    # One-material engine QA fixture combines the real shared wheels with body.
    # It lives only in build; production asset remains wheel-less and 256 square.
    atlas=Image.new('RGBA',(512,256))
    atlas.paste(Image.fromarray(locked[0].texture).convert('RGBA'),(0,0))
    atlas.paste(Image.fromarray(locked[1].texture).convert('RGBA').resize((256,256),Image.Resampling.NEAREST),(256,0))
    atlas.save(output('-qa-atlas.png'))
    values=[]
    for partno,item in enumerate(locked):
        for tri in item.indices:
            for i in tri:
                u,v=item.uvs[i]
                values.append((*item.positions[i],*item.normals[i],u*.5+(.5 if partno else 0),v,1,0,0,1))
    v=np.asarray(values,dtype='<f4')
    with output('-steering-qa.emesh').open('wb') as f:
        f.write(struct.pack('<8I',0x48534D45,2,0,len(v),len(v),1,3,0))
        f.write(v.tobytes())
        f.write(np.arange(len(v),dtype='<u4').tobytes())
        f.write(struct.pack('<4I',0,len(v),0,0))
        f.write(b'qa\0')


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--blockout',action='store_true')
    p.add_argument('--texture-only',action='store_true')
    p.add_argument('--validate-only',action='store_true')
    p.add_argument('--asset-lab',action='store_true',help='use existing binary only; no rebuild or game launch')
    args=p.parse_args()
    PREFIX.parent.mkdir(parents=True,exist_ok=True)
    if args.validate_only:
        validate(args.blockout)
        validate_and_preview(ROOT,'alder_pip',DOOR,DRIVER,parts(),contains,make_preview=False)
        raise SystemExit(0)
    make_texture(args.blockout)
    if not args.texture_only:
        subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','--background','--factory-startup',
                        '--python',str(ROOT/'tools/alder_pip_blender.py'),'--','--mesh',str(MODEL/'body.emesh'),
                        '--blend',str(MODEL/'source.blend')],check=True)
    validate(args.blockout)
    previews(args.blockout)
    validate_and_preview(ROOT,'alder_pip',DOOR,DRIVER,parts(),contains)
    if args.asset_lab:
        for label,mesh,atlas,yaw in [('engine-body',MODEL/'body.emesh',TEXTURE,212),
                                     ('engine-wheels',output('-steering-qa.emesh'),output('-qa-atlas.png'),212),
                                     ('engine-rear',MODEL/'body.emesh',TEXTURE,32)]:
            proc=subprocess.run([str(ROOT/'build/bin/apricot_asset_lab'),'--model',str(mesh),'--texture',str(atlas),
                                 '--yaw',str(yaw),'--frames','60','--screenshot',str(output('-'+label+'.png'))],
                                cwd=ROOT,capture_output=True,text=True)
            output('-'+label+'.log').write_text(proc.stdout+proc.stderr)
            proc.check_returncode()
            assert '0 GL errors' in proc.stdout,proc.stdout
            print(label,'60 frames, 0 GL errors')
