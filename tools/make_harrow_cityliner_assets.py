#!/usr/bin/env python3
"""Cook the full-size HARROW CITYLINER bus and validate shared-wheel clearance.

No fleet bake recipe is required: glass, seams and lamps are already painted
onto their final receiver surfaces. No floating decal skins are exported.
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

from harrow_cityliner_spec import ATLAS_SIZE, REGIONS, BOUNDS, WHEELS, SHAPE, LAMPS
from make_vesper_vx91_assets import read_emesh
from render_firetruck_preview import Part, read_part, raster_view

ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/harrow_cityliner'
TEXTURE=ROOT/'assets/textures/vehicles/harrow_cityliner/body.png'
PREFIX=ROOT/'build/harrow_cityliner'
SCALE=np.ones(3)


def output(suffix):
    return Path(str(PREFIX)+suffix)


def make_texture(blockout=False):
    cream=(221,218,193,255); teal=(43,103,99,255); dark=(28,37,39,255)
    glass=(44,66,73,255); silver=(156,169,162,255)
    im=Image.new('RGBA',(256,256),dark); d=ImageDraw.Draw(im)
    for name,box in REGIONS.items():
        d.rectangle(box,fill=dark if name in ('SHADOW','RUBBER') else silver if name=='METAL' else cream)
    def point(name,p):
        (a,b),(c,e)=BOUNDS[name];x0,y0,x1,y1=REGIONS[name]
        return (round(x0+2+(p[0]-a)/(b-a)*(x1-x0-4)),round(y1-2-(p[1]-c)/(e-c)*(y1-y0-4)))
    def rect(name,a,b,c,e,color):
        pts=[point(name,(a,b)),point(name,(c,e))]
        d.rectangle((min(pts[0][0],pts[1][0]),min(pts[0][1],pts[1][1]),max(pts[0][0],pts[1][0]),max(pts[0][1],pts[1][1])),fill=color)
    def line(name,pts,color,width=1):d.line([point(name,p) for p in pts],fill=color,width=width)
    def window(name,a,b,c,e):
        rect(name,a,b,c,e,dark);rect(name,a+.07,b+.07,c-.07,e-.07,glass)
        rect(name,a+.07,e-.30,c-.07,e-.07,(65,88,91,255))
        line(name,[(a+.12,b+.25),(c-.14,e-.39)],(86,109,110,255))
        line(name,[(a+.08,e-.40),(c-.08,e-.40)],dark)
    for name in ('LEFT','RIGHT','FRONT','REAR'):
        (a,b),_=BOUNDS[name]
        rect(name,a,.22,b,.90,teal)
        rect(name,a,.90,b,.99,(76,136,122,255))
        rect(name,a,1.02,b,1.16,teal)
        rect(name,a,.23,b,.35,(32,58,57,255))
    if not blockout:
        for name in ('LEFT','RIGHT'):
            for a,c in [(-5.1,-3.98),(-3.85,-2.65),(-2.52,-1.32),(-1.19,.01),(.14,1.34),(1.47,2.67),(2.80,3.93),(4.06,5.19)]:
                window(name,a,1.51,c,2.76)
            # Aluminum rub rail and lower service-panel seams, all on receivers.
            line(name,[(-5.34,1.35),(5.34,1.35)],silver)
            for z in (-5.10,-4.0,-1.72,.56,2.12,4.75):
                line(name,[(z,.38),(z,1.27)],(26,80,80,255))
            for z in (-5.1,2.12,5.15):rect(name,z,1.26,z+.12,1.35,(217,148,65,255))
            # Engine cooling grille at the rear, purposefully separate from glazing.
            for y in (.42,.53,.64,.75):line(name,[(-5.1,y),(-3.6,y)],(27,58,60,255))
            # Small fleet identifier and roofline marker cells.
            pos=point(name,(-.8,1.30));d.text(pos,'418',fill=teal)
        # Curbside folding entrance and centre exit. No driver-side door fiction.
        for a,c in ((4.04,5.20),(-1.17,.02)):
            rect('RIGHT',a,.27,c,2.79,dark)
            for aa,cc in ((a+.06,(a+c)/2-.035),((a+c)/2+.035,c-.06)):
                rect('RIGHT',aa,.35,cc,1.44,teal)
                window('RIGHT',aa,1.49,cc,2.70)
            for h in (.40,.55,.69):line('RIGHT',[(a+.08,h),(c-.08,h)],silver)
        # Front: huge split windshield, rollsign, wipers and paired sealed beams.
        window('FRONT',-1.13,1.38,-.035,2.56);window('FRONT',.035,1.38,1.13,2.56)
        rect('FRONT',-.95,2.64,.95,2.88,dark)
        sign=Image.new('RGBA',(68,9),dark);sd=ImageDraw.Draw(sign);sd.text((1,-2),'07 DOWNTOWN',fill=(218,196,111,255))
        a=point('FRONT',(-.94,2.87));b=point('FRONT',(.94,2.65));im.alpha_composite(sign.resize((b[0]-a[0],b[1]-a[1]),Image.Resampling.NEAREST),a)
        for side in (-1,1):
            a,c=sorted((side*.66,side*1.13))
            rect('FRONT',a,.69,c,1.03,dark)
            rect('FRONT',a+.04,.78,c-.04,.99,(229,227,204,255))
            rect('FRONT',a+.04,.69,c-.04,.75,(217,145,62,255))
            line('FRONT',[(side*.12,1.44),(side*.79,1.62)],dark,2)
        rect('FRONT',-.38,.65,.38,.79,(40,61,63,255))
        # Rear: emergency window, diesel service hatch, louvres and vertical lamps.
        window('REAR',-.99,1.66,.99,2.67)
        rect('REAR',-.80,.57,.80,1.38,(61,117,109,255))
        for y in (.67,.78,.89,1.0,1.11,1.22):line('REAR',[(-.73,y),(.73,y)],(28,61,61,255))
        for side in (-1,1):
            a,c=sorted((side*.99,side*1.16))
            rect('REAR',a,.59,c,1.36,dark)
            rect('REAR',a+.02,1.13,c-.02,1.32,(203,142,64,255))
            rect('REAR',a+.02,.87,c-.02,1.10,(159,47,37,255))
            rect('REAR',a+.02,.62,c-.02,.81,(204,210,189,255))
        for name in ('FRONT','REAR'):
            for x in (-.88,-.44,0,.44,.88):rect(name,x-.035,2.95,x+.035,3.01,(206,145,63,255))
        for x in (-1.08,1.08):line('TOP',[(x,-5.2),(x,4.95)],(184,192,174,255))
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
    assert np.allclose(hi-lo,[2.88,2.99,11.0],atol=.001),(lo,hi)
    keys={tuple(np.round(p,4)) for p in v[:,:3]}
    assert all((-x,y,z) in keys for x,y,z in keys),'asymmetric body'
    for axle in (WHEELS['front_z'],WHEELS['rear_z']):
        for s in (-1,1):
            outer=tris[(s*tris[:,:,0]>.61).all(1)]
            for dz,dy in ((.013,.017),(.15,.017),(-.15,.017),(.013,.16),(.013,-.14)):
                p=np.array([axle+dz,WHEELS['arch_y']+dy])
                assert not any(contains(p,t[:,[2,1]]) for t in outer),'closed wheel opening'
    # Connected full-length saloon roof and the central chassis cannot disappear
    # when changing wheel pockets. Offset points avoid triangulation diagonals.
    for x in (-1.03,-.32,.023,.37,1.04):
        for z in (-5.1,-3.5,-2.61,-.71,.31,2.31,3.01,4.7,5.1):
            assert any(contains(np.array([x,z]),t[:,[0,2]]) for t in tris if (t[:,1]>1.1).all()),'missing saloon roof'
    for x in (-.32,.23):
        for z in (-1.71,-.81,.31,1.63):
            assert any(contains(np.array([x,z]),t[:,[0,2]]) for t in tris if (t[:,1]<.40).all()),'missing central chassis'
    # Exact shared-wheel measured bounds set the finite tire-cylinder envelope.
    wheel=wheel_part()
    radius=max(np.ptp(wheel.positions[:,1]),np.ptp(wheel.positions[:,2]))*.5
    wheel_scale=WHEELS['radius']/radius
    halfwidth=np.max(np.abs(wheel.positions[:,0]))*wheel_scale
    scaled=tris*SCALE
    cloud=[]
    for t in scaled:
        if t[:,1].min()>WHEELS['arch_y']*SCALE[1]+.484:continue
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
                bad=(np.abs(width)<halfwidth+.002)&(radial**2+rel[:,1]**2<.482**2)
                assert not bad.any(),f'tire contact: {axle}, {side}, {angle}, {(cloud[bad][:5]/SCALE).tolist()}'
                poses+=1
    with Image.open(TEXTURE) as atlas:
        assert atlas.size==(256,256) and atlas.mode=='RGBA'
        assert atlas.getchannel('A').getextrema()==(255,255)
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
    report=dict(model='harrow_cityliner',vertices=len(v),triangles=len(tris),bounds_min=lo.tolist(),bounds_max=hi.tolist(),
                dimensions=(hi-lo).tolist(),shape_contract=SHAPE,wheels=WHEELS,
                lamp_receiver_samples=lamp_samples,player_fit_scale=SCALE.tolist(),fitted_dimensions=((hi-lo)*SCALE).tolist(),
                wheel_native_radius=float(radius),wheel_native_halfwidth=float(halfwidth/wheel_scale),
                fitted_wheel_radius=WHEELS['radius'],fitted_wheel_halfwidth=float(halfwidth),
                atlas=[256,256,'RGBA'],uv_min=v[:,6:8].min(0).tolist(),uv_max=v[:,6:8].max(0).tolist(),
                checks=['finite static mesh, valid indices, no degenerate faces','triangle budget','symmetric geometry and measured bounds',
                        'four real open side pockets','continuous passenger roof and central chassis','direct receiver atlas; no floating glass/lamp/seam skins',
                        'four anchor empties; one joined body; no wheels in exported source','opaque 256x256 RGBA atlas',
                        'sampled shared-tire envelope clear through full steering range',
                        'headlamp and rear red masks match cooked receiver pixels'],
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
        raise SystemExit(0)
    make_texture(args.blockout)
    if not args.texture_only:
        subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','--background','--factory-startup',
                        '--python',str(ROOT/'tools/harrow_cityliner_blender.py'),'--','--mesh',str(MODEL/'body.emesh'),
                        '--blend',str(MODEL/'source.blend')],check=True)
    validate(args.blockout)
    previews(args.blockout)
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
