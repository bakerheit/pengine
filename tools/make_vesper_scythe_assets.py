#!/usr/bin/env python3
"""Cook the full-size VESPER SCYTHE winged exotic and validate shared-wheel clearance.

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

from vesper_scythe_spec import ATLAS_SIZE, REGIONS, BOUNDS, WHEELS, SHAPE, LAMPS, DOOR, DRIVER
from make_vesper_vx91_assets import read_emesh
from render_firetruck_preview import Part, read_part, raster_view
from validate_vehicle_doors import validate_and_preview

ROOT=Path(__file__).resolve().parents[1]
MODEL=ROOT/'assets/models/vehicles/vesper_scythe'
TEXTURE=ROOT/'assets/textures/vehicles/vesper_scythe/body.png'
PREFIX=ROOT/'build/vesper_scythe'
SCALE=np.ones(3)


def output(suffix):
    return Path(str(PREFIX)+suffix)


def make_texture(blockout=False):
    paint=(167,40,31,255);light=(205,62,42,255);dark=(25,28,30,255)
    glass=(37,52,59,255);silver=(153,163,164,255)
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
        rect('SIDE',-2.245,.18,2.245,.28,(63,31,29,255))
        poly('SIDE',[(-.89,.34),(-.77,.76),(-.26,.66),(-.31,.36)],dark)
        line('SIDE',[(-.73,.70),(-.37,.63)],(70,70,66,255))
        line('SIDE',[(-.22,.34),(-.22,.78),(.77,.67),(.83,.31)],(95,25,22,255))
        rect('SIDE',-.13,.66,.06,.70,dark)
        poly('CABIN',[(-1.13,.90),(-.57,1.10),(.06,1.10),(.99,.73)],dark)
        poly('CABIN',[(-.91,.91),(-.53,1.055),(.035,1.055),(.78,.79)],glass)
        line('CABIN',[(-.47,1.03),(.44,.84)],(74,98,104,255),2)
        line('CABIN',[(-.47,.87),(-.47,1.08)],dark,2)
        for name in ('WINDSHIELD','BACK_GLASS'):
            poly(name,[(-.80,.72),(-.60,1.115),(.60,1.115),(.80,.72)],dark)
            poly(name,[(-.68,.77),(-.53,1.065),(.53,1.065),(.68,.77)],glass)
            line(name,[(-.44,1.03),(.49,.83)],(74,98,104,255),2)
        for x in (-.71,.71):
            rect('TOP',x-.20,1.65,x+.20,2.04,(104,30,25,255))
            rect('TOP',x-.18,1.67,x+.18,2.01,light)
        rect('TOP',-.64,-2.15,.64,-1.18,dark)
        for z in (-2.05,-1.90,-1.75,-1.60,-1.45,-1.30):line('TOP',[(-.59,z),(.59,z)],(89,88,79,255),2)
        rect('FRONT',-.92,.32,.92,.47,dark)
        for side in (-1,1):
            a,c=sorted((side*.46,side*.86));rect('FRONT',a,.345,c,.455,(232,227,209,255))
            a,c=sorted((side*.85,side*.93));rect('FRONT',a,.33,c,.39,(214,138,57,255))
        rect('FRONT',-.33,.23,.33,.30,dark)
        rect('REAR',-.91,.49,.91,.78,dark)
        for side in (-1,1):
            a,c=sorted((side*.52,side*.89));rect('REAR',a,.55,c,.71,(169,42,31,255))
            a,c=sorted((side*.82,side*.90));rect('REAR',a,.72,c,.78,(203,139,46,255))
        rect('REAR',-.25,.49,.25,.63,(194,202,179,255))
        for x in (-.37,.37):rect('REAR',x-.10,.23,x+.10,.30,dark)
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
    assert np.allclose(hi-lo,[2.18,1.01,4.65],atol=.001),(lo,hi)
    keys={tuple(np.round(p,4)) for p in v[:,:3]}
    assert all((-x,y,z) in keys for x,y,z in keys),'asymmetric body'
    for axle in (WHEELS['front_z'],WHEELS['rear_z']):
        for s in (-1,1):
            outer=tris[(s*tris[:,:,0]>.74).all(1)]
            for dz,dy in ((.013,.017),(.15,.017),(-.15,.017),(.013,.16),(.013,-.14)):
                p=np.array([axle+dz,WHEELS['arch_y']+dy])
                assert not any(contains(p,t[:,[2,1]]) for t in outer),'closed wheel opening'
    # Preserve a broad hood and complete hatch/cabin roof across both axles.
    for x in (-.63,-.23,.023,.31,.64):
        for z in (-1.9,-1.4,-.7,.03,.8,1.31,1.90):
            assert any(contains(np.array([x,z]),t[:,[0,2]]) for t in tris if (t[:,1]>.48).all()),'missing hood/cabin coverage'
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
    report=dict(model='vesper_scythe',vertices=len(v),triangles=len(tris),bounds_min=lo.tolist(),bounds_max=hi.tolist(),
                dimensions=(hi-lo).tolist(),shape_contract=SHAPE,wheels=WHEELS,
                lamp_receiver_samples=lamp_samples,player_fit_scale=SCALE.tolist(),fitted_dimensions=((hi-lo)*SCALE).tolist(),
                wheel_native_radius=float(radius),wheel_native_halfwidth=float(halfwidth/wheel_scale),
                fitted_wheel_radius=WHEELS['radius'],fitted_wheel_halfwidth=float(halfwidth),
                atlas=[256,256,'RGBA'],uv_min=v[:,6:8].min(0).tolist(),uv_max=v[:,6:8].max(0).tolist(),
                checks=['finite static mesh, valid indices, no degenerate faces','triangle budget','symmetric geometry and measured bounds',
                        'four real open side pockets','continuous hood/cabin roof and central chassis','direct receiver atlas; no floating glass/lamp/seam skins',
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
        validate_and_preview(ROOT,'vesper_scythe',DOOR,DRIVER,parts(),contains,make_preview=False)
        raise SystemExit(0)
    make_texture(args.blockout)
    if not args.texture_only:
        subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','--background','--factory-startup',
                        '--python',str(ROOT/'tools/vesper_scythe_blender.py'),'--','--mesh',str(MODEL/'body.emesh'),
                        '--blend',str(MODEL/'source.blend')],check=True)
    validate(args.blockout)
    previews(args.blockout)
    validate_and_preview(ROOT,'vesper_scythe',DOOR,DRIVER,parts(),contains)
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
